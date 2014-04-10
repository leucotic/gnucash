/********************************************************************\
 * qofbackend.c -- utility routines for dealing with the data backend  *
 * Copyright (C) 2000 Linas Vepstas <linas@linas.org>               *
 * Copyright (C) 2004-5 Neil Williams <linux@codehelp.co.uk>        *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

#define _GNU_SOURCE
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <regex.h>
#include <glib.h>
#include <gmodule.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <errno.h>
#include "qofbackend-p.h"

static QofLogModule log_module = QOF_MOD_BACKEND;

#define QOF_CONFIG_DESC    "desc"
#define QOF_CONFIG_TIP     "tip"

/********************************************************************\
 * error handling                                                   *
\********************************************************************/

void 
qof_backend_set_error (QofBackend *be, QofBackendError err)
{
   if (!be) return;

   /* use stack-push semantics. Only the earliest error counts */
   if (ERR_BACKEND_NO_ERR != be->last_err) return;
   be->last_err = err;
}

QofBackendError 
qof_backend_get_error (QofBackend *be)
{
   QofBackendError err;
   if (!be) return ERR_BACKEND_NO_BACKEND;

   /* use 'stack-pop' semantics */
   err = be->last_err;
   be->last_err = ERR_BACKEND_NO_ERR;
   return err;
}

void
qof_backend_set_message (QofBackend *be, const char *format, ...) 
{
   va_list args;
   char * buffer;
   
   if (!be) return;
  
   /* If there's already something here, free it */
   if (be->error_msg) g_free(be->error_msg);

   if (!format) {
       be->error_msg = NULL;
       return;
   }

   va_start(args, format);
   buffer = (char *)g_strdup_vprintf(format, args);
   va_end(args);

   be->error_msg = buffer;
}

/* This should always return a valid char * */
char *
qof_backend_get_message (QofBackend *be) 
{
   char * msg;
   
   if (!be) return g_strdup("ERR_BACKEND_NO_BACKEND");
   if (!be->error_msg) return NULL;

   /* 
    * Just return the contents of the error_msg and then set it to
    * NULL.  This is necessary, because the Backends don't seem to
    * have a destroy_backend function to take care if freeing stuff
    * up.  The calling function should free the copy.
    * Also, this is consistent with the qof_backend_get_error() popping.
    */

   msg = be->error_msg;
   be->error_msg = NULL;
   return msg;
}

/***********************************************************************/
/* Get a clean backend */
void
qof_backend_init(QofBackend *be)
{
    be->session_begin = NULL;
    be->session_end = NULL;
    be->destroy_backend = NULL;

    be->load = NULL;

    be->begin = NULL;
    be->commit = NULL;
    be->rollback = NULL;

    be->compile_query = NULL;
    be->free_query = NULL;
    be->run_query = NULL;

    be->sync = NULL;
	be->load_config = NULL;

    be->events_pending = NULL;
    be->process_events = NULL;

    be->last_err = ERR_BACKEND_NO_ERR;
    if (be->error_msg) g_free (be->error_msg);
    be->error_msg = NULL;
    be->percentage = NULL;
	be->backend_configuration = kvp_frame_new();

#ifdef GNUCASH_MAJOR_VERSION
    /* XXX remove these */
    be->fullpath = NULL;
    be->price_lookup = NULL;
    be->export = NULL;
#endif
}

void
qof_backend_run_begin(QofBackend *be, QofInstance *inst)
{
	if(!be || !inst) { return; }
	if(!be->begin) { return; }
	(be->begin) (be, inst);
}

gboolean
qof_backend_begin_exists(QofBackend *be)
{
	if(be->begin) { return TRUE; }
	else { return FALSE; }
}

void
qof_backend_run_commit(QofBackend *be, QofInstance *inst)
{
	if(!be || !inst) { return; }
	if(!be->commit) { return; }
	(be->commit) (be, inst);
}

/* =========== Backend Configuration ================ */

void qof_backend_prepare_frame(QofBackend *be)
{
	g_return_if_fail(be);
	if(!kvp_frame_is_empty(be->backend_configuration)) {
		kvp_frame_delete(be->backend_configuration);
		be->backend_configuration = kvp_frame_new();
	}
	be->config_count = 0;
}

void qof_backend_prepare_option(QofBackend *be, QofBackendOption *option)
{
	KvpValue *value;
	gchar *temp;
	gint count;

	g_return_if_fail(be || option);
	count = be->config_count;
	count++;
	value = NULL;
	ENTER (" %d", count);
	switch (option->type)
	{
		case KVP_TYPE_GINT64   : {
			value = kvp_value_new_gint64(*(gint64*)option->value);
			break; 
		}
		case KVP_TYPE_DOUBLE   : { 
			value = kvp_value_new_double(*(double*)option->value);
			break; 
		}
		case KVP_TYPE_NUMERIC  : {
			value = kvp_value_new_numeric(*(gnc_numeric*)option->value);
			break; 
		}
		case KVP_TYPE_STRING   : {
			value = kvp_value_new_string((const char*)option->value);
			break;
		}
		case KVP_TYPE_GUID     : { break; } /* unsupported */
		case KVP_TYPE_TIMESPEC : {
			value = kvp_value_new_timespec(*(Timespec*)option->value);
			break;
		}
		case KVP_TYPE_BINARY   : { break; } /* unsupported */
		case KVP_TYPE_GLIST    : { break; } /* unsupported */
		case KVP_TYPE_FRAME    : { break; } /* unsupported */
	}
	if(value) {
		temp = g_strdup_printf("/%s", option->option_name);
		kvp_frame_set_value(be->backend_configuration, temp, value);
		PINFO (" setting value at %s", temp);
		g_free(temp);
		temp = g_strdup_printf("/%s/%s", QOF_CONFIG_DESC, option->option_name);
		PINFO (" setting description %s at %s", option->description, temp);
		kvp_frame_set_string(be->backend_configuration, temp, option->description);
		PINFO (" check= %s", kvp_frame_get_string(be->backend_configuration, temp));
		g_free(temp);
		temp = g_strdup_printf("/%s/%s", QOF_CONFIG_TIP, option->option_name);
		PINFO (" setting tooltip %s at %s", option->tooltip, temp);
		kvp_frame_set_string(be->backend_configuration, temp, option->tooltip);
		PINFO (" check= %s", kvp_frame_get_string(be->backend_configuration, temp));
		g_free(temp);
		/* only increment the counter if successful */
		be->config_count = count;
	}
	LEAVE (" ");
}

KvpFrame* qof_backend_complete_frame(QofBackend *be)
{
	g_return_val_if_fail(be, NULL);
	be->config_count = 0;
	return be->backend_configuration;
}

struct config_iterate {
	QofBackendOptionCB fcn;
	gpointer           data;
	gint               count;
	KvpFrame          *recursive;
};

static void
config_foreach_cb (const char *key, KvpValue *value, gpointer data)
{
	QofBackendOption option;
	gint64 int64;
	double db;
	gnc_numeric num;
	Timespec ts;
	gchar *parent;
	struct config_iterate *helper;

	g_return_if_fail(key || value || data);
	helper = (struct config_iterate*)data;
	if(!helper->recursive) { PERR (" no parent frame"); return;	}
	// skip the presets.
	if(0 == safe_strcmp(key, QOF_CONFIG_DESC)) { return; }
	if(0 == safe_strcmp(key, QOF_CONFIG_TIP)) { return; }
	ENTER (" key=%s", key);
	option.option_name = key;
	option.type = kvp_value_get_type(value);
	if(!option.type) { return; }
	switch (option.type)
	{
		case KVP_TYPE_GINT64   : {
			int64 = kvp_value_get_gint64(value);
			option.value = (gpointer)&int64;
			break; 
		}
		case KVP_TYPE_DOUBLE   : {
			db = kvp_value_get_double(value);
			option.value = (gpointer)&db;
			break; 
		}
		case KVP_TYPE_NUMERIC  : {
			num = kvp_value_get_numeric(value);
			option.value = (gpointer)&num;
			break; 
		}
		case KVP_TYPE_STRING   : {
			option.value = (gpointer)kvp_value_get_string(value);
			break;
		}
		case KVP_TYPE_GUID     : { break; } /* unsupported */
		case KVP_TYPE_TIMESPEC : {
			ts = kvp_value_get_timespec(value);
			option.value = (gpointer)&ts;
			break;
		}
		case KVP_TYPE_BINARY   : { break; } /* unsupported */
		case KVP_TYPE_GLIST    : { break; } /* unsupported */
		case KVP_TYPE_FRAME    : { break; } /* unsupported */
	}
	parent = g_strdup_printf("/%s/%s", QOF_CONFIG_DESC, key);
	option.description = kvp_frame_get_string(helper->recursive, parent);
	g_free(parent);
	parent = g_strdup_printf("/%s/%s", QOF_CONFIG_TIP, key);
	option.tooltip = kvp_frame_get_string(helper->recursive, parent);
	helper->count++;
	helper->fcn (&option, helper->data);
	LEAVE (" desc=%s tip=%s", option.description, option.tooltip);
}

void qof_backend_option_foreach(KvpFrame *config, QofBackendOptionCB cb, gpointer data)
{
	struct config_iterate helper;

	if(!config || !cb) { return; }
	ENTER (" ");
	helper.fcn = cb;
	helper.count = 1;
	helper.data = data;
	helper.recursive = config;
	kvp_frame_for_each_slot(config, config_foreach_cb, &helper);
	LEAVE (" ");
}

void
qof_backend_load_config(QofBackend *be, KvpFrame *config)
{
	if(!be || !config) { return; }
	if(!be->load_config) { return; }
	(be->load_config) (be, config);
}

KvpFrame*
qof_backend_get_config(QofBackend *be)
{
	if(!be) { return NULL; }
	if(!be->get_config) { return NULL; }
	return (be->get_config) (be);
}

gboolean
qof_backend_commit_exists(QofBackend *be)
{
	if(!be) { return FALSE; }
	if(be->commit) { return TRUE; }
	else { return FALSE; }
}

gboolean
qof_begin_edit(QofInstance *inst)
{
  QofBackend * be;

  if (!inst) { return FALSE; }
  (inst->editlevel)++;
  if (1 < inst->editlevel) { return FALSE; }
  if (0 >= inst->editlevel) { inst->editlevel = 1; }
  be = qof_book_get_backend (inst->book);
    if (be && qof_backend_begin_exists(be)) {
     qof_backend_run_begin(be, inst);
  } else { inst->dirty = TRUE; }
  return TRUE;
}

gboolean qof_commit_edit(QofInstance *inst)
{
  QofBackend * be;

  if (!inst) { return FALSE; }
  (inst->editlevel)--;
  if (0 < inst->editlevel) { return FALSE; }
  if ((-1 == inst->editlevel) && inst->dirty)
  {
    be = qof_book_get_backend ((inst)->book);
    if (be && qof_backend_begin_exists(be)) {
     qof_backend_run_begin(be, inst);
    }
    inst->editlevel = 0;
  }
  if (0 > inst->editlevel) { inst->editlevel = 0; }
  return TRUE;
}

gboolean
qof_load_backend_library (const char *directory, 
				const char* filename, const char* init_fcn)
{
	struct stat sbuf;
	gchar *fullpath;
	typedef void (* backend_init) (void);
	GModule *backend;
	backend_init gmod_init;
	gpointer g;

	g_return_val_if_fail(g_module_supported(), FALSE);
	fullpath = g_module_build_path(directory, filename);
	PINFO (" fullpath=%s", fullpath);
	g_return_val_if_fail((stat(fullpath, &sbuf) == 0), FALSE);
	backend = g_module_open(fullpath, G_MODULE_BIND_LAZY);
	if(!backend) { 
		g_message ("%s: %s\n", PACKAGE, g_module_error ());
		return FALSE;
	}
	g = &gmod_init;
	if (!g_module_symbol (backend, init_fcn, g))
	{
		g_message ("%s: %s\n", PACKAGE, g_module_error ());
		return FALSE;
	}
	g_module_make_resident(backend);
	gmod_init();
	return TRUE;
}

/************************* END OF FILE ********************************/