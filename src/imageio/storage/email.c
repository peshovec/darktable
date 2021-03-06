/*
    This file is part of darktable,
    copyright (c) 2009--2011 Henrik Andersson.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/darktable.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "common/imageio_module.h"
#include "common/imageio.h"
#include "common/imageio_storage.h"
#include "control/control.h"
#include "control/conf.h"
#include "gui/gtk.h"
#include "dtgtk/button.h"
#include "dtgtk/paint.h"
#include <stdio.h>
#include <stdlib.h>

DT_MODULE(2)

typedef struct _email_attachment_t
{
  uint32_t imgid; // The image id of exported image
  gchar *file;    // Full filename of exported image
} _email_attachment_t;

// saved params
typedef struct dt_imageio_email_t
{
  char filename[DT_MAX_PATH_FOR_PARAMS];
  GList *images;
} dt_imageio_email_t;


const char *name(const struct dt_imageio_module_storage_t *self)
{
  return _("send as email");
}

void *legacy_params(dt_imageio_module_storage_t *self, const void *const old_params,
                    const size_t old_params_size, const int old_version, const int new_version,
                    size_t *new_size)
{
  if(old_version == 1 && new_version == 2)
  {
    typedef struct dt_imageio_email_v1_t
    {
      char filename[1024];
      GList *images;
    } dt_imageio_email_v1_t;

    dt_imageio_email_t *n = (dt_imageio_email_t *)malloc(sizeof(dt_imageio_email_t));
    dt_imageio_email_v1_t *o = (dt_imageio_email_v1_t *)old_params;

    g_strlcpy(n->filename, o->filename, sizeof(n->filename));

    *new_size = self->params_size(self);
    return n;
  }
  return NULL;
}

int recommended_dimension(struct dt_imageio_module_storage_t *self, uint32_t *width, uint32_t *height)
{
  *width = 1536;
  *height = 1536;
  return 1;
}


void gui_init(dt_imageio_module_storage_t *self)
{
}

void gui_cleanup(dt_imageio_module_storage_t *self)
{
  free(self->gui_data);
}

void gui_reset(dt_imageio_module_storage_t *self)
{
}

int store(dt_imageio_module_storage_t *self, dt_imageio_module_data_t *sdata, const int imgid,
          dt_imageio_module_format_t *format, dt_imageio_module_data_t *fdata, const int num, const int total,
          const gboolean high_quality)
{
  dt_imageio_email_t *d = (dt_imageio_email_t *)sdata;

  _email_attachment_t *attachment = (_email_attachment_t *)g_malloc(sizeof(_email_attachment_t));
  attachment->imgid = imgid;

  /* construct a temporary file name */
  char tmpdir[PATH_MAX] = { 0 };
  dt_loc_get_tmp_dir(tmpdir, sizeof(tmpdir));

  char dirname[PATH_MAX] = { 0 };
  gboolean from_cache = FALSE;
  dt_image_full_path(imgid, dirname, sizeof(dirname), &from_cache);
  gchar *filename = g_path_get_basename(dirname);

  g_strlcpy(dirname, filename, sizeof(dirname));

  dt_image_path_append_version(imgid, dirname, sizeof(dirname));

  gchar *end = g_strrstr(dirname, ".") + 1;

  if(end) *end = '\0';

  g_strlcat(dirname, format->extension(fdata), sizeof(dirname));

  // set exported filename

  attachment->file = g_build_filename(tmpdir, dirname, (char *)NULL);

  if(dt_imageio_export(imgid, attachment->file, format, fdata, high_quality, FALSE, self, sdata) != 0)
  {
    fprintf(stderr, "[imageio_storage_email] could not export to file: `%s'!\n", attachment->file);
    dt_control_log(_("could not export to file `%s'!"), attachment->file);
    g_free(attachment);
    g_free(filename);
    return 1;
  }


  char *trunc = attachment->file + strlen(attachment->file) - 32;
  if(trunc < attachment->file) trunc = attachment->file;
  dt_control_log(_("%d/%d exported to `%s%s'"), num, total, trunc != filename ? ".." : "", trunc);

#ifdef _OPENMP // store can be called in parallel, so synch access to shared memory
#pragma omp critical
#endif
  d->images = g_list_append(d->images, attachment);

  g_free(filename);

  return 0;
}

size_t params_size(dt_imageio_module_storage_t *self)
{
  return sizeof(dt_imageio_email_t) - sizeof(GList *);
}

void init(dt_imageio_module_storage_t *self)
{
}

void *get_params(dt_imageio_module_storage_t *self)
{
  dt_imageio_email_t *d = (dt_imageio_email_t *)g_malloc0(sizeof(dt_imageio_email_t));
  return d;
}

int set_params(dt_imageio_module_storage_t *self, const void *params, const int size)
{
  if(size != self->params_size(self)) return 1;
  return 0;
}

void free_params(dt_imageio_module_storage_t *self, dt_imageio_module_data_t *params)
{
  free(params);
}

void finalize_store(dt_imageio_module_storage_t *self, dt_imageio_module_data_t *params)
{
  dt_imageio_email_t *d = (dt_imageio_email_t *)params;

  // All images are exported, generate a mailto uri and startup default mail client
  gchar uri[4096] = { 0 };
  gchar body[4096] = { 0 };
  gchar attachments[4096] = { 0 };
  gchar *uriFormat = "xdg-email --subject \"%s\" --body \"%s\" %s &"; // subject, body format
  const gchar *subject = _("images exported from darktable");
  const gchar *imageBodyFormat = " - %s (%s)\\n"; // filename, exif oneliner
  gchar *attachmentFormat = " --attach \"%s\"";   // list of attachments format
  gchar *attachmentSeparator = "";

  while(d->images)
  {
    gchar exif[256] = { 0 };
    _email_attachment_t *attachment = (_email_attachment_t *)d->images->data;
    gchar *filename = g_path_get_basename(attachment->file);
    const dt_image_t *img = dt_image_cache_read_get(darktable.image_cache, attachment->imgid);
    dt_image_print_exif(img, exif, sizeof(exif));
    g_snprintf(body + strlen(body), sizeof(body) - strlen(body), imageBodyFormat, filename, exif);
    g_free(filename);

    if(*attachments)
      g_snprintf(attachments + strlen(attachments), sizeof(attachments) - strlen(attachments), "%s",
                 attachmentSeparator);

    g_snprintf(attachments + strlen(attachments), sizeof(attachments) - strlen(attachments), attachmentFormat,
               attachment->file);
    // Free attachment item and remove
    dt_image_cache_read_release(darktable.image_cache, img);
    g_free(d->images->data);
    d->images = g_list_remove(d->images, d->images->data);
  }

  // build uri and launch before we quit...
  g_snprintf(uri, sizeof(uri), uriFormat, subject, body, attachments);

  fprintf(stderr, "[email] launching `%s'\n", uri);
  if(system(uri) < 0)
  {
    dt_control_log(_("could not launch email client!"));
  }
}

int supported(struct dt_imageio_module_storage_t *storage, struct dt_imageio_module_format_t *format)
{
  const char *mime = format->mime(NULL);
  if(mime[0] == '\0') // this seems to be the copy format
    return 0;

  return 1;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
