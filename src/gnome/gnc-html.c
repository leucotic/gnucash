/********************************************************************
 * gnc-html.c -- display HTML with some special gnucash tags.       *
 *                                                                  *
 * Copyright (C) 2000 Bill Gribble <grib@billgribble.com>           *
 * Copyright (C) 2001 Linas Vepstas <linas@linas.org>               *
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
 ********************************************************************/

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <ghttp_ssl.h>
#endif

#include <ghttp.h>
#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/gtkhtml-embedded.h>
#ifdef USE_GUPPI
#include <libguppitank/guppi-tank.h>
#endif
#include <gnome.h>
#include <regex.h>
#include <glib.h>
#include <guile/gh.h>

#include "Account.h"
#include "Group.h"
#include "RegWindow.h"
#include "File.h"
#include "FileBox.h"
#include "FileDialog.h"
#include "dialog-utils.h"
#include "window-register.h"
#include "print-session.h"
#include "gnc-engine-util.h"
#include "gnc-gpg.h"
#include "gnc-html.h"
#include "gnc-html-history.h"
#include "gnc-html-embedded.h"
#include "query-user.h"
#include "window-help.h"
#include "window-report.h"

struct _gnc_html {
  GtkWidget * container;
  GtkWidget * html;

  gchar     * current_link;         /* link under mouse pointer */

  URLType   base_type;              /* base of URL (path - filename) */
  gchar     * base_location;

  /* callbacks */
  GncHTMLUrltypeCB  urltype_cb;
  GncHTMLLoadCB     load_cb;
  GncHTMLFlyoverCB  flyover_cb;
  GncHTMLButtonCB   button_cb;

  GList             * requests;     /* outstanding ghttp requests */

  gpointer          flyover_cb_data;
  gpointer          load_cb_data;
  gpointer          button_cb_data;
  
  struct _gnc_html_history * history; 
};

struct request_info {
  gchar         * uri;
  ghttp_request * request;
  GtkHTMLStream * handle;
};

/* This static indicates the debugging module that this .o belongs to.  */
static short module = MOD_HTML;

static char error_404[] = 
"<html><body><h3>Not found</h3><p>The specified URL could not be loaded.</body></html>";

static char error_start[] = "<html><body><h3>Error</h3><p>There was an error loading the specified URL. <p>Error message: <p> ";
static char error_end[] = "</body></html>";

static char error_report[] = 
"<html><body><h3>Report error</h3><p>An error occurred while running the report.</body></html>";


/********************************************************************
 * gnc_html_parse_url
 * this takes a URL and determines the protocol type, location, and
 * possible anchor name from the URL.
 ********************************************************************/

URLType
gnc_html_parse_url(gnc_html * html, const gchar * url, 
                   char ** url_location, char ** url_label) {
  char        uri_rexp[] = "^(([^:]*):)?([^#]+)?(#(.*))?$";
  regex_t     compiled;
  regmatch_t  match[6];
  char        * protocol=NULL, * path=NULL, * label=NULL;
  int         found_protocol=0, found_path=0, found_label=0; 
  URLType     retval;   

  regcomp(&compiled, uri_rexp, REG_EXTENDED);

  if(!regexec(&compiled, url, 6, match, 0)) {
    if(match[2].rm_so != -1) {
      protocol = g_new0(char, match[2].rm_eo - match[2].rm_so + 1);
      strncpy(protocol, url + match[2].rm_so, 
              match[2].rm_eo - match[2].rm_so);
      protocol[match[2].rm_eo - match[2].rm_so] = 0;
      found_protocol = 1;      
    }
    if(match[3].rm_so != -1) {
      path = g_new0(char, match[3].rm_eo - match[3].rm_so + 1);
      strncpy(path, url+match[3].rm_so, 
              match[3].rm_eo - match[3].rm_so);
      path[match[3].rm_eo - match[3].rm_so] = 0;
      found_path = 1;
    }
    if(match[5].rm_so != -1) {
      label = g_new0(char, match[5].rm_eo - match[5].rm_so + 1);
      strncpy(label, url+match[5].rm_so, 
              match[5].rm_eo - match[5].rm_so);
      label[match[5].rm_eo - match[5].rm_so] = 0;
      found_label = 1;
    }
  }

  regfree(&compiled);

  if(found_protocol) {
    if(!strcmp(protocol, "file")) {
      retval = URL_TYPE_FILE;
    }
    else if(!strcmp(protocol, "http")) {
      retval = URL_TYPE_HTTP;
    }
    else if(!strcmp(protocol, "ftp")) {
      retval = URL_TYPE_FTP;
    }
    else if(!strcmp(protocol, "https")) {
      retval = URL_TYPE_SECURE;
    }
    else if(!strcmp(protocol, "gnc-register")) {
      retval = URL_TYPE_REGISTER;
    }
    else if(!strcmp(protocol, "gnc-report")) {
      retval = URL_TYPE_REPORT;
    }
    else if(!strcmp(protocol, "gnc-scm")) {
      retval = URL_TYPE_SCHEME;
    }
    else if(!strcmp(protocol, "gnc-help")) {
      retval = URL_TYPE_HELP;
    }
    else {
      PWARN("unhandled URL type for '%s'", url);
      retval = URL_TYPE_OTHER;
    }
  }
  else if(found_label && !found_path) {
    retval = URL_TYPE_JUMP;
  }
  else {
    retval = html->base_type;
  }
  
  g_free(protocol);
 
  switch(retval) {
  case URL_TYPE_FILE:    
    if(!found_protocol && path && html->base_location) {
      *url_location = g_new0(char, 
                             strlen(path) + strlen(html->base_location) + 1);
      *url_location[0] = 0;

      strcpy(*url_location, html->base_location);      
      strcat(*url_location, "/");
      strcat(*url_location, path);
      
      g_free(path);
    }
    else {
      *url_location = path;
    }
    break;

  case URL_TYPE_JUMP:
    *url_location = NULL;
    g_free(path);
    break;

  case URL_TYPE_OTHER:
  default:
    if(!found_protocol && path && html->base_location) {
      *url_location = g_new0(char, 
                             strlen(path) + strlen(html->base_location) + 1);
      *url_location[0] = 0;
      strcpy(*url_location, html->base_location);
      strcat(*url_location, path);
      g_free(path);
    }
    else {
      *url_location = path;
    }
    break;
  }
  
  *url_label = label;
  return retval;
}


static char * 
extract_base_name(URLType type, const gchar * path) {
  char       http_rexp[] = "^(//[^/]*)(/.*)?$";
  char       other_rexp[] = "^(.*)(/([^/]*))?$";
  regex_t    compiled_h, compiled_o;
  regmatch_t match[4];
  char       * machine=NULL, * location = NULL, * base=NULL;
  int        free_location = 0;

  regcomp(&compiled_h, http_rexp, REG_EXTENDED);
  regcomp(&compiled_o, other_rexp, REG_EXTENDED);

  if(!path) return NULL;

  switch(type) {
  case URL_TYPE_HTTP:
  case URL_TYPE_SECURE:
  case URL_TYPE_FTP:
    if(!regexec(&compiled_h, path, 4, match, 0)) {
      if(match[1].rm_so != -1) {
        machine = g_new0(char, strlen(path) + 2);
        strncpy(machine, path+match[1].rm_so, 
                match[1].rm_eo - match[1].rm_so);
      } 
      if(match[2].rm_so != -1) {
        location = g_new0(char, match[2].rm_eo - match[2].rm_so + 1);
        strncpy(location, path+match[2].rm_so, 
                match[2].rm_eo - match[2].rm_so);
        free_location = 1;
      }
    }  
    break;
  default:
    location = g_strdup(path);
  }
   
  if(location) {
    if(!regexec(&compiled_o, location, 4, match, 0)) {
      if(match[2].rm_so != -1) {
        base = g_new0(char,  match[1].rm_eo - match[1].rm_so + 1);
        strncpy(base, path+match[1].rm_so, 
                match[1].rm_eo - match[1].rm_so);
      }
    }
  }
  
  regfree(&compiled_h);
  regfree(&compiled_o);

  if(free_location) {
    g_free(location);
  }

  if(machine) {
    strcat(machine, "/");
    if(base) { 
      strcat(machine, base);
    }
    g_free(base);
    return machine;
  }
  else {
    g_free(machine);
    return base;
  }
}

static char * url_type_names[] = {
  "file:", "", "http:", "ftp:", "https:", 
  "gnc-register:", "gnc-report:", "gnc-scm:",
  "gnc-help:", ""
};


static gchar  *
rebuild_url(URLType type, const gchar * location, const gchar * label) {
  if(label) {
    return g_strdup_printf("%s%s#%s", url_type_names[type], 
                           (location ? location : ""), label);
  }
  else {
    return g_strdup_printf("%s%s", url_type_names[type], 
                           (location ? location : ""));
  }
}

static guint ghttp_callback_tag = 0;
static int ghttp_callback_enabled = FALSE;

static gint
ghttp_check_callback(gpointer data) {
  gnc_html            * html = data;
  GList               * current; 
  ghttp_status        status;
  struct request_info * req;
  URLType             type;
  char                * location = NULL;
  char                * label = NULL;
        
  /* walk the request list to deal with any complete requests */
  for(current = html->requests; current; current = current->next) {
    req = current->data;
    
    status = ghttp_process(req->request);
    switch(status) {
    case ghttp_done:
      if (ghttp_get_body_len(req->request) > 0) {      
        {
          /* hack alert  FIXME:
           * This code tries to see if the returned body is
           * in fact gnc xml code. If it seems to be, then we 
           * load it as data, rather than loading it into the 
           * gtkhtml widget.  My gut impression is that we should 
           * probably be doing this somewhere else, some other
           * way, not here .... But I can't think of anything 
           * better for now. -- linas
           */
          const char * bufp = ghttp_get_body(req->request);
          bufp += strspn (bufp, " /t/v/f/n/r");
          if (!strncmp (bufp, "<?xml version", 13)) {
            gncFileOpenFile ((char *) req->uri);
            return TRUE;
          } 
        }
        
        gtk_html_write(GTK_HTML(html->html), 
                       req->handle, 
                       ghttp_get_body(req->request), 
                       ghttp_get_body_len(req->request));
        gtk_html_end(GTK_HTML(html->html), req->handle, GTK_HTML_STREAM_OK);

        type = gnc_html_parse_url(html, req->uri, &location, &label);
        if(label) {
          gtk_html_jump_to_anchor(GTK_HTML(html->html), label);
        }
      }
      else {
        gtk_html_write(GTK_HTML(html->html), req->handle, error_404, 
                       strlen(error_404));
        gtk_html_end(GTK_HTML(html->html), req->handle, GTK_HTML_STREAM_ERROR);
      }
      ghttp_request_destroy(req->request);
      req->request   = NULL;
      req->handle    = NULL;
      current->data  = NULL;
      g_free(req);
      break;

    case ghttp_error:
      gtk_html_write(GTK_HTML(html->html), req->handle, error_start, 
                     strlen(error_start));
      gtk_html_write(GTK_HTML(html->html), req->handle, 
                     ghttp_get_error(req->request), 
                     strlen(ghttp_get_error(req->request)));
      gtk_html_write(GTK_HTML(html->html), req->handle, error_end, 
                     strlen(error_end));
      gtk_html_end(GTK_HTML(html->html), req->handle, GTK_HTML_STREAM_ERROR);
      ghttp_request_destroy(req->request);
      req->request   = NULL;
      req->handle    = NULL;
      current->data  = NULL;
      g_free(req);
      break;

    case ghttp_not_done:
      break;
    }
  }
  
  /* walk the list again to remove dead requests */
  current = html->requests;
  while(current) {
    if(current->data == NULL) {
      html->requests = g_list_remove_link(html->requests, current);
      current = html->requests;
    }
    else {
      current = current->next;
    }
  }

  /* if all requests are done, disable the timeout */
  if(html->requests == NULL) {
    ghttp_callback_enabled = FALSE;
    ghttp_callback_tag = 0;
    return FALSE;
  }
  else {
    return TRUE;
  }
}


#ifdef HAVE_OPENSSL
static int
gnc_html_certificate_check_cb(ghttp_request * req, X509 * cert, 
                              void * user_data) {
  PINFO("checking SSL certificate...");
  X509_print_fp(stdout, cert);
  PINFO(" ... done\n");
  return TRUE;
}
#endif

static void 
gnc_html_start_request(gnc_html * html, gchar * uri, GtkHTMLStream * handle) {
  
  struct request_info * info = g_new0(struct request_info, 1);

  info->request = ghttp_request_new();
  info->handle  = handle;
  info->uri  = g_strdup (uri);
#ifdef HAVE_OPENSSL
  ghttp_enable_ssl(info->request);
  ghttp_set_ssl_certificate_callback(info->request, 
                                     gnc_html_certificate_check_cb, 
                                     (void *)html);
#endif
  ghttp_set_uri(info->request, uri);
  ghttp_set_header(info->request, http_hdr_User_Agent, 
                   "gnucash/1.5 (Financial Browser for Linux; http://gnucash.org)");
  ghttp_set_sync(info->request, ghttp_async);
  ghttp_prepare(info->request);
  ghttp_process(info->request);

  html->requests = g_list_append(html->requests, info);
  
  /* start the gtk timeout if not started */
  if(!ghttp_callback_enabled) {
    ghttp_callback_tag = 
      gtk_timeout_add(100, ghttp_check_callback, (gpointer)html);
    ghttp_callback_enabled = TRUE;
  }
}


/********************************************************************
 * gnc_html_load_to_stream : actually do the work of loading the HTML
 * or binary data referenced by a URL and feeding it into the GtkHTML
 * widget.
 ********************************************************************/

static void
gnc_html_load_to_stream(gnc_html * html, GtkHTMLStream * handle,
                        URLType type, const gchar * location, 
                        const gchar * label) {
  int           fsize;
  char          * fdata = NULL;
  char          * fullurl;
  int           id;
  SCM           run_report;
  SCM           scmtext;

  if(!html) {
    return;
  }
  
  switch(type) {
  case URL_TYPE_HELP:
  case URL_TYPE_FILE:
    fsize = gncReadFile(location, &fdata);
    if(fsize > 0) {
      gtk_html_write(GTK_HTML(html->html), handle, fdata, fsize);
      gtk_html_end(GTK_HTML(html->html), handle, GTK_HTML_STREAM_OK);      
    }
    else {
      gtk_html_write(GTK_HTML(html->html), handle, error_404, 
                     strlen(error_404));
      gtk_html_end(GTK_HTML(html->html), handle, GTK_HTML_STREAM_ERROR);
    }
    g_free(fdata);
    if(label) {
      gtk_html_jump_to_anchor(GTK_HTML(html->html), label);
    }
    break;
    
  case URL_TYPE_HTTP:
  case URL_TYPE_FTP:
  case URL_TYPE_SECURE:
    fullurl = rebuild_url(type, location, label);
    gnc_html_start_request(html, fullurl, handle);    
    break;
    
  case URL_TYPE_REPORT:
    run_report = gh_eval_str("gnc:report-run");

    if(!strncmp("id=", location, 3)) {
      /* get the report ID */
      sscanf(location+3, "%d", &id);
      
      /* get the HTML text */ 
      scmtext = gh_call1(run_report, gh_int2scm(id));
      if(scmtext == SCM_BOOL_F) {
        gtk_html_write(GTK_HTML(html->html), handle, 
                       error_report, strlen(error_report));        
      }
      else {
        fdata = gh_scm2newstr(scmtext, &fsize);
        if(fdata) {
          gtk_html_write(GTK_HTML(html->html), handle, fdata, fsize);
          TRACE ("%s", fdata);
          free(fdata);
          fdata = NULL;
          fsize = 0;
          if(label) {
            gtk_html_jump_to_anchor(GTK_HTML(html->html), label);
          }
        }
        else {
          gtk_html_write(GTK_HTML(html->html), handle, error_404, 
                         strlen(error_404));
          PWARN("report HTML generator failed.");
        }
      }
    }

    gtk_html_end(GTK_HTML(html->html), handle, GTK_HTML_STREAM_OK);
    break;
    
  case URL_TYPE_REGISTER:
  case URL_TYPE_SCHEME:
  default:
    PWARN("load_to_stream for inappropriate type\n"
          "\turl = '%s#%s'\n", location, label);
    gtk_html_write(GTK_HTML(html->html), handle, error_404, 
                   strlen(error_404));
    gtk_html_end(GTK_HTML(html->html), handle, GTK_HTML_STREAM_ERROR);
    break;
    
  }
}


/********************************************************************
 * gnc_html_link_clicked_cb - called when user left-clicks on html
 * anchor. 
 ********************************************************************/

static void 
gnc_html_link_clicked_cb(GtkHTML * html, const gchar * url, gpointer data) {
  URLType   type;
  char      * location = NULL;
  char      * label = NULL;
  gnc_html  * gnchtml = (gnc_html *)data;

  type = gnc_html_parse_url(gnchtml, url, &location, &label);
  gnc_html_show_url(gnchtml, type, location, label, 0);
  g_free(location);
  g_free(label);
}


/********************************************************************
 * gnc_html_url_requested_cb - called when a URL needs to be 
 * loaded within the loading of a page (embedded image).
 ********************************************************************/

static void 
gnc_html_url_requested_cb(GtkHTML * html, char * url,
                          GtkHTMLStream * handle, gpointer data) {
  URLType       type;
  char          * location=NULL;
  char          * label=NULL;
  gnc_html      * gnchtml = (gnc_html *)data;

  type = gnc_html_parse_url(gnchtml, url, &location, &label);
  gnc_html_load_to_stream(gnchtml, handle, type, location, label);
  g_free(location);
  g_free(label);
}


#ifdef USE_GUPPI
static void 
gnc_html_guppi_print_cb(GtkHTMLEmbedded * eb, GnomePrintContext * pc,
                        gpointer data) {
  GtkWidget   * w = data;
  GuppiObject * o = gtk_object_get_user_data(GTK_OBJECT(w));

  /* this is a magical scaling factor (gtkhtml and guppi assume different 
   * screen resolutions) */
  gnome_print_scale(pc, 0.6944, 0.6944);
  guppi_object_print(o, pc);
}

static void 
gnc_html_guppi_redraw_cb(GtkHTMLEmbedded * eb,
                         GdkPixmap * pix, GdkGC * gc, gint x, gint y, 
                         gpointer data) {
  /* nothing special to do */
}
#endif /* USE_GUPPI */

static char * 
unescape_newlines(const gchar * in) {
  const char * ip = in;
  char * retval = g_strdup(in);
  char * op = retval;

  for(ip=in; *ip; ip++) {
    if((*ip == '\\') && (*(ip+1)=='n')) {
      *op = '\012';
      op++;
      ip++;
    }
    else {
      *op = *ip;
      op++;
    }
  }
  *op = 0;
  return retval;
}


/********************************************************************
 * gnc_html_object_requested_cb - called when an applet needs to be
 * loaded.  
 ********************************************************************/

static int
gnc_html_object_requested_cb(GtkHTML * html, GtkHTMLEmbedded * eb,
                             gpointer data) {
  GtkWidget * widg = NULL;
  gnc_html  * gnchtml = data; 
  int retval = FALSE;

  if(!strcmp(eb->classid, "gnc-guppi-pie")) {
#ifdef USE_GUPPI
    widg = gnc_html_embedded_piechart(gnchtml, eb->width, eb->height, 
                                      eb->params); 
#endif /* USE_GUPPI */
    if(widg) {
      gtk_widget_show_all(widg);
      gtk_container_add(GTK_CONTAINER(eb), widg);
      gtk_widget_set_usize(GTK_WIDGET(eb), eb->width, eb->height);
      retval = TRUE;
    }
    else {
      retval = FALSE;
    }
  }
  else if(!strcmp(eb->classid, "gnc-guppi-bar")) {
#ifdef USE_GUPPI
    widg = gnc_html_embedded_barchart(gnchtml, eb->width, eb->height, 
                                      eb->params); 
#endif /* USE_GUPPI */
    if(widg) {
      gtk_widget_show_all(widg);
      gtk_container_add(GTK_CONTAINER(eb), widg);
      gtk_widget_set_usize(GTK_WIDGET(eb), eb->width, eb->height);
      retval = TRUE;
    }
    else {
      retval = FALSE;
    }
  }
  else if(!strcmp(eb->classid, "gnc-guppi-scatter")) {
#ifdef USE_GUPPI
    widg = gnc_html_embedded_scatter(gnchtml, eb->width, eb->height, 
                                     eb->params); 
#endif /* USE_GUPPI */
    if(widg) {
      gtk_widget_show_all(widg);
      gtk_container_add(GTK_CONTAINER(eb), widg);
      gtk_widget_set_usize(GTK_WIDGET(eb), eb->width, eb->height);
      retval = TRUE;
    }
    else {
      retval = FALSE;
    }
  }
  else if(!strcmp(eb->classid, "gnc-account-tree")) {
    widg = gnc_html_embedded_account_tree(gnchtml, eb->width, eb->height, 
                                          eb->params); 
    if(widg) {
      gtk_widget_show_all(widg);
      gtk_container_add(GTK_CONTAINER(eb), widg);
      gtk_widget_set_usize(GTK_WIDGET(eb), eb->width, eb->height);
      retval = TRUE;
    }
    else {
      retval = FALSE;
    }
  }
#if USE_GPG
  else if(!strcmp(eb->classid, "gnc-crypted-html")) {
    /* we just want to take the data and stuff it into the widget,
       blowing away the active streams.  crypted-html contains a
       complete HTML page. */
    char * cryptext  = unescape_newlines(eb->data);
    char * cleartext = gnc_gpg_decrypt(cryptext, strlen(cryptext));
    GtkHTMLStream * handle;
    
    if(cleartext && cleartext[0]) {
      handle = gtk_html_begin(html);
      gtk_html_write(html, handle, cleartext, strlen(cleartext));
      gtk_html_end(html, handle, GTK_HTML_STREAM_OK);
      retval = TRUE;
    }
    else {
      retval = FALSE;
    }
    g_free(cleartext);
    g_free(cryptext);
  }
#endif /* USE_GPG */

#if 0 && defined(USE_GUPPI)
  if(widg) {
    gtk_signal_connect(GTK_OBJECT(eb), "draw_gdk",
                       GTK_SIGNAL_FUNC(gnc_html_guppi_redraw_cb),
                       widg);
    gtk_signal_connect(GTK_OBJECT(eb), "draw_print",
                       GTK_SIGNAL_FUNC(gnc_html_guppi_print_cb),
                       widg);
  }
#endif

  return retval;
}


/********************************************************************
 * gnc_html_on_url_cb - called when user rolls over html anchor
 ********************************************************************/

static void 
gnc_html_on_url_cb(GtkHTML * html, const gchar * url, gpointer data) {
  gnc_html * gnchtml = (gnc_html *) data;

  g_free(gnchtml->current_link);
  gnchtml->current_link = g_strdup(url);
  if(gnchtml->flyover_cb) {
    (gnchtml->flyover_cb)(gnchtml, url, gnchtml->flyover_cb_data);
  }
}


/********************************************************************
 * gnc_html_set_base_cb 
 ********************************************************************/

static void 
gnc_html_set_base_cb(GtkHTML * gtkhtml, const gchar * base, 
                     gpointer data) {
  gnc_html * html = (gnc_html *)data;
  URLType  type;
  char     * location = NULL;
  char     * label = NULL;

  type = gnc_html_parse_url(html, base, &location, &label);

  g_free(html->base_location);
  g_free(label);

  html->base_type     = type;
  html->base_location = location;
  
}


/********************************************************************
 * gnc_html_key_cb
 ********************************************************************/

static gboolean
gnc_html_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data) {
  gnc_html * hw = (gnc_html *) data;
  
  GtkAdjustment * vadj = 
    gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(hw->container));
  GtkAdjustment * hadj = 
    gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(hw->container));
  
  gfloat        v_value = vadj->value;
  gfloat        h_value = hadj->value;

  switch (event->keyval)
  {
    case GDK_KP_Left:
    case GDK_Left:
      h_value -= hadj->step_increment;
      break;
    case GDK_KP_Right:
    case GDK_Right:
      h_value += hadj->step_increment;
      break;
    case GDK_KP_Up:
    case GDK_Up:
      v_value -= vadj->step_increment;
      break;
    case GDK_KP_Down:
    case GDK_Down:
      v_value += vadj->step_increment;
      break;
    case GDK_KP_Page_Up:
    case GDK_Page_Up:
      v_value -= vadj->page_increment;
      break;
    case GDK_KP_Page_Down:
    case GDK_Page_Down:
    case GDK_space:
      v_value += vadj->page_increment;
      break;
    case GDK_KP_Home:
    case GDK_Home:
      v_value = vadj->lower;
      break;
    case GDK_KP_End:
    case GDK_End:
      v_value = vadj->upper;
      break;
    default:
      return FALSE;
  }

  v_value = CLAMP(v_value, vadj->lower, vadj->upper - vadj->page_size);
  h_value = CLAMP(h_value, hadj->lower, hadj->upper - hadj->page_size);

  gtk_adjustment_set_value(vadj, v_value);
  gtk_adjustment_set_value(hadj, h_value);

  return TRUE;
}


/********************************************************************
 * gnc_html_button_press_cb
 * mouse button callback (if any)
 ********************************************************************/

static int
gnc_html_button_press_cb(GtkWidget * widg, GdkEventButton * event,
                         gpointer user_data) {
  gnc_html * html = user_data;

  if(html->button_cb) {
    (html->button_cb)(html, event, html->button_cb_data);
    return TRUE;
  }
  else {
    return FALSE;
  }
}


/********************************************************************
 * gnc_html_button_submit_cb
 * form submission callback
 ********************************************************************/

static int
gnc_html_submit_cb(GtkHTML * html, const gchar * method, 
                   const gchar * action, const gchar * encoding,
                   gpointer user_data) {
  if(!strcasecmp(method, "get")) {
    PINFO("GET submit: m='%s', a='%s', e='%s'",
           method, action, encoding);
  }
  else if(!strcasecmp(method, "post")) {
    PINFO("POST submit: m='%s', a='%s', e='%s'",
           method, action, encoding);
  }
  return TRUE;
}


/********************************************************************
 * gnc_html_open_register
 * open a register window 
 ********************************************************************/

static void
gnc_html_open_register(gnc_html * html, const gchar * location) {
  Account   * acct;
  RegWindow * reg;

  /* href="gnc-register:account=My Bank Account" */
  if(!strncmp("account=", location, 8)) {
    acct = xaccGetAccountFromFullName(gncGetCurrentGroup(),
                                      location+8, 
                                      gnc_get_account_separator());
    reg = regWindowSimple(acct);
    gnc_register_raise(reg);
  }
  else {
    gnc_warning_dialog(_("Badly formed gnc-register: URL."));
  }
}


/********************************************************************
 * gnc_html_open_report
 * open a report window 
 ********************************************************************/

static void
gnc_html_open_report(gnc_html * html, const gchar * location,
                     const gchar * label, int newwin) {
  gnc_report_window * rwin;
  GtkHTMLStream * stream;

  /* make a new window if necessary */ 
  if(newwin) {
    rwin = gnc_report_window_new(NULL);
    html = gnc_report_window_get_html(rwin);
  }

  gnc_html_history_append(html->history,
                          gnc_html_history_node_new(URL_TYPE_REPORT, 
                                                    location, label));
  
  g_free(html->base_location);
  html->base_type     = URL_TYPE_FILE;
  html->base_location = NULL;

  stream = gtk_html_begin(GTK_HTML(html->html));
  gnc_html_load_to_stream(html, stream, URL_TYPE_REPORT, location, label);
}


/********************************************************************
 * gnc_html_open_help
 * open a help window 
 ********************************************************************/

static void
gnc_html_open_help(gnc_html * html, const gchar * location,
                   const gchar * label, int newwin) {
  gnc_help_window * help = NULL;
  
  if(newwin) {
    help = gnc_help_window_new();
    gnc_help_window_show_help(help, location, label);
  }
  else {
    gnc_html_show_url(html, URL_TYPE_FILE, location, label, 0);
  }      
}


/********************************************************************
 * gnc_html_open_scm
 * insert some scheme-generated HTML
 ********************************************************************/

static void
gnc_html_open_scm(gnc_html * html, const gchar * location,
                  const gchar * label, int newwin) {
  PINFO("location='%s'", location);
}


/********************************************************************
 * gnc_html_show_url 
 * 
 * open a URL.  This is called when the user clicks a link or 
 * for the creator of the gnc_html window to explicitly request 
 * a URL. 
 ********************************************************************/

void 
gnc_html_show_url(gnc_html * html, URLType type, 
                  const gchar * location, const gchar * label,
                  int newwin_hint) {
  
  GtkHTMLStream * handle;
  int           newwin;

  /* make sure it's OK to show this URL type in this window */
  if(newwin_hint == 0) {
    newwin = !((html->urltype_cb)(type));
  }
  else {
    newwin = 1;
  }

  switch(type) {
  case URL_TYPE_REGISTER:
    gnc_html_open_register(html, location);
    break;

  case URL_TYPE_REPORT:
    gnc_html_open_report(html, location, label, newwin);
    break;
    
  case URL_TYPE_HELP:
    gnc_html_open_help(html, location, label, newwin);
    break;
    
  case URL_TYPE_SCHEME:
    gnc_html_open_scm(html, location, label, newwin);
    break;
    
  case URL_TYPE_JUMP:
    gtk_html_jump_to_anchor(GTK_HTML(html->html), label);
    break;
    
  case URL_TYPE_HTTP:
  case URL_TYPE_FTP:
  case URL_TYPE_SECURE:
  case URL_TYPE_FILE:
    html->base_type     = type;

    if(html->base_location) g_free(html->base_location);
    html->base_location = extract_base_name(type, location);
    
    /* FIXME : handle newwin = 1 */
    gnc_html_history_append(html->history,
                            gnc_html_history_node_new(type, 
                                                      location, label));
    handle = gtk_html_begin(GTK_HTML(html->html));
    gnc_html_load_to_stream(html, handle, type, location, label);
    break;
    
  default:
    break;
  }

  if(html->load_cb) {
    (html->load_cb)(html, type, location, label, html->load_cb_data);
  }
}


/********************************************************************
 * gnc_html_reload
 * reload the current page 
 ********************************************************************/

void
gnc_html_reload(gnc_html * html) {
  gnc_html_history_node * n = gnc_html_history_get_current(html->history);
  if(n) {
    gnc_html_show_url(html, n->type, n->location, n->label, 0);
  }
}


/********************************************************************\
 * gnc_html_new
 * create and set up a new gtkhtml widget.
\********************************************************************/

gnc_html * 
gnc_html_new(void) {
  gnc_html * retval = g_new0(gnc_html, 1);
  
  retval->container = gtk_scrolled_window_new(NULL, NULL);
  retval->html = gtk_html_new();

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(retval->container),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);

  gtk_container_add(GTK_CONTAINER(retval->container), 
                    GTK_WIDGET(retval->html));
  
  retval->history = gnc_html_history_new();

  gtk_widget_ref (retval->container);
  gtk_object_sink (GTK_OBJECT (retval->container));

  /* signals */
  gtk_signal_connect(GTK_OBJECT(retval->html), "url_requested",
                     GTK_SIGNAL_FUNC(gnc_html_url_requested_cb),
                     (gpointer)retval);
  
  gtk_signal_connect(GTK_OBJECT(retval->html), "on_url",
                     GTK_SIGNAL_FUNC(gnc_html_on_url_cb),
                     (gpointer)retval);
  
  gtk_signal_connect(GTK_OBJECT(retval->html), "set_base",
                     GTK_SIGNAL_FUNC(gnc_html_set_base_cb),
                     (gpointer)retval);
  
  gtk_signal_connect(GTK_OBJECT(retval->html), "link_clicked",
                     GTK_SIGNAL_FUNC(gnc_html_link_clicked_cb),
                     (gpointer)retval);
  
  gtk_signal_connect (GTK_OBJECT (retval->html), "object_requested",
                      GTK_SIGNAL_FUNC (gnc_html_object_requested_cb), 
                      (gpointer)retval);

  gtk_signal_connect (GTK_OBJECT (retval->html), "button_press_event",
                      GTK_SIGNAL_FUNC (gnc_html_button_press_cb), 
                      (gpointer)retval);
  
  gtk_signal_connect (GTK_OBJECT(retval->html), "key_press_event", 
                      GTK_SIGNAL_FUNC(gnc_html_key_cb), (gpointer)retval);
  
  gtk_signal_connect (GTK_OBJECT(retval->html), "submit", 
                      GTK_SIGNAL_FUNC(gnc_html_submit_cb), (gpointer)retval);
  
  gtk_widget_show_all(GTK_WIDGET(retval->html));
  
  gtk_html_load_empty(GTK_HTML(retval->html));
  
  return retval;
}

/********************************************************************\
 * gnc_html_cancel
 * cancel any outstanding HTML fetch requests. 
\********************************************************************/
void
gnc_html_cancel(gnc_html * html) {
  GList * current;
  
  if(ghttp_callback_enabled == TRUE) {
    gtk_timeout_remove(ghttp_callback_tag);
    ghttp_callback_enabled = FALSE;
    ghttp_callback_tag = 0;

    /* go through and destroy all the requests */
    for(current = html->requests; current; current = current->next) {
      if(current->data) {
        struct request_info * r = current->data;
        ghttp_request_destroy(r->request);
        g_free(r->uri);
        g_free(r);
        current->data = NULL;
      }
    }

    /* free the list backbone */
    g_list_free(html->requests);
    html->requests = NULL;
  }  
}


/********************************************************************\
 * gnc_html_destroy
 * destroy the struct
\********************************************************************/

void
gnc_html_destroy(gnc_html * html) {

  if(!html) return;

  /* cancel any outstanding HTTP requests */
  gnc_html_cancel(html);
  
  gnc_html_history_destroy(html->history);

  gtk_widget_destroy(html->container);
  gtk_widget_unref(html->container);

  g_free(html->current_link);
  g_free(html->base_location);

  html->container     = NULL;
  html->html          = NULL;
  html->history       = NULL;
  html->current_link  = NULL;
  html->base_location = NULL;

  g_free(html);
}

void
gnc_html_set_urltype_cb(gnc_html * html, GncHTMLUrltypeCB urltype_cb) {
  html->urltype_cb = urltype_cb;
}

void
gnc_html_set_load_cb(gnc_html * html, GncHTMLLoadCB load_cb,
                     gpointer data) {
  html->load_cb = load_cb;
  html->load_cb_data = data;
}

void
gnc_html_set_flyover_cb(gnc_html * html, GncHTMLFlyoverCB flyover_cb,
                        gpointer data) {
  html->flyover_cb       = flyover_cb;
  html->flyover_cb_data  = data;
}

void
gnc_html_set_button_cb(gnc_html * html, GncHTMLButtonCB button_cb,
                        gpointer data) {
  html->button_cb       = button_cb;
  html->button_cb_data  = data;
}

/* ------------------------------------------------------- */

static gboolean 
raw_html_receiver (gpointer     engine,
               const gchar *data,
               guint        len,
               gpointer     user_data)
{
  FILE *fh = (FILE *) user_data;
  fwrite (data, len, 1, fh);
  return TRUE;
}

void
gnc_html_export(gnc_html * html) 
{
  const char *filepath;
  FILE *fh;

  filepath = fileBox (_("Save HTML To File"), NULL, NULL);
  PINFO (" user selected file=%s\n", filepath);
  fh = fopen (filepath, "w");
  if (NULL == fh)
  {
     const char *fmt = _("Could not open the file\n"
                         "     %s\n%s");
     char *buf = g_strdup_printf (fmt, filepath, strerror (errno));
     gnc_error_dialog (buf);
     if (buf) g_free (buf);
     return;
  }

  gtk_html_save (GTK_HTML(html->html), raw_html_receiver, fh);
  fclose (fh);
}

/* ------------------------------------------------------- */

void
gnc_html_print(gnc_html * html) {
  PrintSession * ps = gnc_print_session_create();
  
  gtk_html_print(GTK_HTML(html->html),
                 GNOME_PRINT_CONTEXT(ps->meta));
  gnc_print_session_done(ps);
  gnc_print_session_print(ps);
}

gnc_html_history * 
gnc_html_get_history(gnc_html * html) {
  if (!html) return NULL;
  return html->history;
}


GtkWidget * 
gnc_html_get_widget(gnc_html * html) {
  if (!html) return NULL;
  return html->container;
}


#ifdef _TEST_GNC_HTML_
int
main(int argc, char ** argv) {
  
  GtkWidget * wind;
  gnc_html  * html;
 
  gnome_init("test", "1.0", argc, argv);
  gdk_rgb_init();
  gtk_widget_set_default_colormap (gdk_rgb_get_cmap ());
  gtk_widget_set_default_visual (gdk_rgb_get_visual ());
  
  wind = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  html = gnc_html_new();

  gtk_container_add(GTK_CONTAINER(wind), GTK_WIDGET(html->container));
  gtk_widget_show_all(wind);

  gnc_html_load_file(html, "test.html");

  gtk_main();

}
#endif

