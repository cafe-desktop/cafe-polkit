/*
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#ifndef __POLKIT_CAFE_AUTHENTICATION_DIALOG_H
#define __POLKIT_CAFE_AUTHENTICATION_DIALOG_H

#include <ctk/ctk.h>
#include <polkit/polkit.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POLKIT_CAFE_TYPE_AUTHENTICATION_DIALOG            (polkit_cafe_authentication_dialog_get_type ())
#define POLKIT_CAFE_AUTHENTICATION_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), POLKIT_CAFE_TYPE_AUTHENTICATION_DIALOG, PolkitCafeAuthenticationDialog))
#define POLKIT_CAFE_AUTHENTICATION_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), POLKIT_CAFE_TYPE_AUTHENTICATION_DIALOG, PolkitCafeAuthenticationDialogClass))
#define POLKIT_CAFE_IS_AUTHENTICATION_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), POLKIT_CAFE_TYPE_AUTHENTICATION_DIALOG))
#define POLKIT_CAFE_IS_AUTHENTICATION_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), POLKIT_CAFE_TYPE_AUTHENTICATION_DIALOG))

typedef struct _PolkitCafeAuthenticationDialog        PolkitCafeAuthenticationDialog;
typedef struct _PolkitCafeAuthenticationDialogClass   PolkitCafeAuthenticationDialogClass;
typedef struct _PolkitCafeAuthenticationDialogPrivate PolkitCafeAuthenticationDialogPrivate;

struct _PolkitCafeAuthenticationDialog
{
  CtkDialog parent_instance;
  PolkitCafeAuthenticationDialogPrivate *priv;
};

struct _PolkitCafeAuthenticationDialogClass
{
  CtkDialogClass parent_class;
};

GType      polkit_cafe_authentication_dialog_get_type                      (void);
CtkWidget *polkit_cafe_authentication_dialog_new                           (const gchar    *action_id,
                                                                             const gchar    *vendor,
                                                                             const gchar    *vendor_url,
                                                                             const gchar    *icon_name,
                                                                             const gchar    *message_markup,
                                                                             PolkitDetails  *details,
                                                                             gchar         **users);
gchar     *polkit_cafe_authentication_dialog_get_selected_user             (PolkitCafeAuthenticationDialog *dialog);
gboolean   polkit_cafe_authentication_dialog_run_until_user_is_selected    (PolkitCafeAuthenticationDialog *dialog);
gchar     *polkit_cafe_authentication_dialog_run_until_response_for_prompt (PolkitCafeAuthenticationDialog *dialog,
                                                                             const gchar                     *prompt,
                                                                             gboolean                         echo_chars,
                                                                             gboolean                        *was_cancelled,
                                                                             gboolean                        *new_user_selected);
gboolean   polkit_cafe_authentication_dialog_cancel                        (PolkitCafeAuthenticationDialog *dialog);
void       polkit_cafe_authentication_dialog_indicate_error                (PolkitCafeAuthenticationDialog *dialog);
void       polkit_cafe_authentication_dialog_set_info_message              (PolkitCafeAuthenticationDialog *dialog,
                                                                             const gchar                     *info_markup);

#ifdef __cplusplus
}
#endif

#endif /* __POLKIT_CAFE_AUTHENTICATION_DIALOG_H */
