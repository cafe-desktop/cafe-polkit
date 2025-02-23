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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <errno.h>

#include <glib/gi18n-lib.h>
#include <ctk/ctk.h>

#include "polkitcafeauthenticationdialog.h"

#define RESPONSE_USER_SELECTED 1001

struct _PolkitCafeAuthenticationDialogPrivate
{
  CtkWidget *user_combobox;
  CtkWidget *prompt_label;
  CtkWidget *password_entry;
  CtkWidget *auth_button;
  CtkWidget *cancel_button;
  CtkWidget *info_label;
  CtkWidget *grid_password;

  gchar *message;
  gchar *action_id;
  gchar *vendor;
  gchar *vendor_url;
  gchar *icon_name;
  PolkitDetails *details;

  gchar **users;
  gchar *selected_user;

  gboolean is_running;

  CtkListStore *store;
};

G_DEFINE_TYPE_WITH_PRIVATE (PolkitCafeAuthenticationDialog, polkit_cafe_authentication_dialog, CTK_TYPE_DIALOG);

enum {
  PROP_0,
  PROP_ACTION_ID,
  PROP_VENDOR,
  PROP_VENDOR_URL,
  PROP_ICON_NAME,
  PROP_MESSAGE,
  PROP_DETAILS,
  PROP_USERS,
  PROP_SELECTED_USER,
};

enum {
  PIXBUF_COL,
  TEXT_COL,
  USERNAME_COL,
  N_COL
};

static void
user_combobox_set_sensitive (CtkCellLayout   *cell_layout G_GNUC_UNUSED,
			     CtkCellRenderer *cell,
			     CtkTreeModel    *tree_model,
			     CtkTreeIter     *iter,
			     gpointer         user_data G_GNUC_UNUSED)
{
  CtkTreePath *path;
  gint *indices;
  gboolean sensitive;

  path = ctk_tree_model_get_path (tree_model, iter);
  indices = ctk_tree_path_get_indices (path);
  if (indices[0] == 0)
    sensitive = FALSE;
  else
    sensitive = TRUE;
  ctk_tree_path_free (path);

  g_object_set (cell, "sensitive", sensitive, NULL);
}

static void
user_combobox_changed (CtkComboBox *widget,
                       gpointer     user_data)
{
  PolkitCafeAuthenticationDialog *dialog = POLKIT_CAFE_AUTHENTICATION_DIALOG (user_data);
  CtkTreeIter iter;
  gchar *user_name;

  if (ctk_combo_box_get_active_iter (CTK_COMBO_BOX (widget), &iter))
    {
      ctk_tree_model_get (CTK_TREE_MODEL (dialog->priv->store), &iter, USERNAME_COL, &user_name, -1);

      g_free (dialog->priv->selected_user);
      dialog->priv->selected_user = user_name;

      g_object_notify (G_OBJECT (dialog), "selected-user");

      ctk_dialog_response (CTK_DIALOG (dialog), RESPONSE_USER_SELECTED);

      /* make the password entry and Authenticate button sensitive again */
      ctk_widget_set_sensitive (dialog->priv->prompt_label, TRUE);
      ctk_widget_set_sensitive (dialog->priv->password_entry, TRUE);
      ctk_widget_set_sensitive (dialog->priv->auth_button, TRUE);
    }
}

#if HAVE_ACCOUNTSSERVICE
static GdkPixbuf *
get_user_icon (char *username)
{
  GError *error;
  GDBusConnection *connection;
  GVariant *find_user_result;
  GVariant *get_icon_result;
  GVariant *icon_result_variant;
  const gchar *user_path;
  const gchar *icon_filename;
  GdkPixbuf *pixbuf = NULL;

  error = NULL;
  connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);

  if (connection == NULL)
    {
      g_warning ("Unable to connect to system bus: %s", error->message);
      g_error_free (error);
      return NULL;
    }

  find_user_result = g_dbus_connection_call_sync (connection,
                                          "org.freedesktop.Accounts",
                                          "/org/freedesktop/Accounts",
                                          "org.freedesktop.Accounts",
                                          "FindUserByName",
                                          g_variant_new ("(s)",
                                          username),
                                          G_VARIANT_TYPE ("(o)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);

  if (find_user_result == NULL)
    {
      g_warning ("Accounts couldn't find user: %s", error->message);
      g_error_free (error);
      return NULL;
    }

  user_path = g_variant_get_string (g_variant_get_child_value (find_user_result, 0),
                                    NULL);

  get_icon_result = g_dbus_connection_call_sync (connection,
                                                 "org.freedesktop.Accounts",
                                                 user_path,
                                                 "org.freedesktop.DBus.Properties",
                                                 "Get",
                                                 g_variant_new ("(ss)",
                                                                "org.freedesktop.Accounts.User",
                                                                "IconFile"),
                                                 G_VARIANT_TYPE ("(v)"),
                                                 G_DBUS_CALL_FLAGS_NONE,
                                                 -1,
                                                 NULL,
                                                 &error);

  g_variant_unref (find_user_result);

  if (get_icon_result == NULL)
    {
      g_warning ("Accounts couldn't find user icon: %s", error->message);
      g_error_free (error);
      return NULL;
    }

  g_variant_get_child (get_icon_result, 0, "v", &icon_result_variant);
  icon_filename = g_variant_get_string (icon_result_variant, NULL);

  if (icon_filename == NULL)
    {
      g_warning ("Accounts didn't return a valid filename for user icon");
    }
  else
    {
      /* TODO: we probably shouldn't hard-code the size to 16x16 */
      pixbuf = gdk_pixbuf_new_from_file_at_size (icon_filename,
                                                 16,
                                                 16,
                                                 &error);
      if (pixbuf == NULL)
        {
          g_warning ("Couldn't open user icon: %s", error->message);
          g_error_free (error);
        }
    }

  g_variant_unref (icon_result_variant);
  g_variant_unref (get_icon_result);

  return pixbuf;
}
#else
static GdkPixbuf *
get_user_icon (char *username)
{
  GdkPixbuf *pixbuf = NULL;
  struct passwd *passwd;

  passwd = getpwnam (username);
  if (passwd->pw_dir != NULL)
    {
      gchar *path;
      path = g_strdup_printf ("%s/.face", passwd->pw_dir);
      /* TODO: we probably shouldn't hard-code the size to 16x16 */
      pixbuf = gdk_pixbuf_new_from_file_at_scale (path, 16, 16, TRUE, NULL);
      g_free (path);
    }

  return pixbuf;
}
#endif /* HAVE_ACCOUNTSSERVICE */

static void
create_user_combobox (PolkitCafeAuthenticationDialog *dialog)
{
  int n, i, selected_index = 0;
  CtkComboBox *combo;
  CtkTreeIter iter;
  CtkCellRenderer *renderer;

  /* if we've already built the list of admin users once, then avoid
   * doing it again.. (this is mainly used when the user entered the
   * wrong password and the dialog is recycled)
   */
  if (dialog->priv->store != NULL)
    return;

  combo = CTK_COMBO_BOX (dialog->priv->user_combobox);
  dialog->priv->store = ctk_list_store_new (3, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING);

  ctk_list_store_append (dialog->priv->store, &iter);
  ctk_list_store_set (dialog->priv->store, &iter,
                      PIXBUF_COL, NULL,
                      TEXT_COL, _("Select user..."),
                      USERNAME_COL, NULL,
                      -1);


  /* For each user */
  for (i = 0, n = 0; dialog->priv->users[n] != NULL; n++)
  {
      gchar *gecos;
      gchar *real_name;
      GdkPixbuf *pixbuf = NULL;
      struct passwd *passwd;

      /* we're single threaded so this is fine */
      errno = 0;
      passwd = getpwnam (dialog->priv->users[n]);
      if (passwd == NULL)
        {
          g_warning ("Error doing getpwnam(\"%s\"): %s", dialog->priv->users[n], strerror (errno));
          continue;
        }

      if (passwd->pw_gecos != NULL)
        gecos = g_locale_to_utf8 (passwd->pw_gecos, -1, NULL, NULL, NULL);
      else
        gecos = NULL;

      if (gecos != NULL && strlen (gecos) > 0)
        {
          gchar *first_comma;
          first_comma = strchr (gecos, ',');
          if (first_comma != NULL)
            *first_comma = '\0';
        }
      if (gecos != NULL && strlen (gecos) > 0 && strcmp (gecos, dialog->priv->users[n]) != 0)
        real_name = g_strdup_printf (_("%s (%s)"), gecos, dialog->priv->users[n]);
       else
         real_name = g_strdup (dialog->priv->users[n]);
      g_free (gecos);

      /* Load users face */
      pixbuf = get_user_icon (dialog->priv->users[n]);

      /* fall back to stock_person icon */
      if (pixbuf == NULL)
        {
          pixbuf = ctk_icon_theme_load_icon (ctk_icon_theme_get_default (),
                                             "stock_person",
                                             CTK_ICON_SIZE_MENU,
                                             0,
                                             NULL);
        }

      ctk_list_store_append (dialog->priv->store, &iter);
      ctk_list_store_set (dialog->priv->store, &iter,
                          PIXBUF_COL, pixbuf,
                          TEXT_COL, real_name,
                          USERNAME_COL, dialog->priv->users[n],
                          -1);

      i++;
      if (passwd->pw_uid == getuid ())
        {
          selected_index = i;
          g_free (dialog->priv->selected_user);
          dialog->priv->selected_user = g_strdup (dialog->priv->users[n]);
        }

      g_free (real_name);
      g_object_unref (pixbuf);
    }

  ctk_combo_box_set_model (combo, CTK_TREE_MODEL (dialog->priv->store));

  renderer = ctk_cell_renderer_pixbuf_new ();
  ctk_cell_layout_pack_start (CTK_CELL_LAYOUT (combo), renderer, FALSE);
  ctk_cell_layout_set_attributes (CTK_CELL_LAYOUT (combo),
                                  renderer,
                                  "pixbuf", PIXBUF_COL,
                                  NULL);
  ctk_cell_layout_set_cell_data_func (CTK_CELL_LAYOUT (combo),
                                      renderer,
                                      user_combobox_set_sensitive,
                                      NULL, NULL);

  renderer = ctk_cell_renderer_text_new ();
  ctk_cell_layout_pack_start (CTK_CELL_LAYOUT (combo), renderer, TRUE);
  ctk_cell_layout_set_attributes (CTK_CELL_LAYOUT (combo),
                                  renderer,
                                  "text", TEXT_COL,
                                  NULL);
  ctk_cell_layout_set_cell_data_func (CTK_CELL_LAYOUT (combo),
                                      renderer,
                                      user_combobox_set_sensitive,
                                      NULL, NULL);

  /* Select the default user */
  ctk_combo_box_set_active (CTK_COMBO_BOX (combo), selected_index);

  /* Listen when a new user is selected */
  g_signal_connect (CTK_WIDGET (combo),
                    "changed",
                    G_CALLBACK (user_combobox_changed),
                    dialog);
}

static CtkWidget *
get_image (PolkitCafeAuthenticationDialog *dialog)
{
  GdkPixbuf *pixbuf;
  GdkPixbuf *copy_pixbuf;
  GdkPixbuf *vendor_pixbuf;
  CtkWidget *image;

  pixbuf = NULL;
  copy_pixbuf = NULL;
  vendor_pixbuf = NULL;
  image = NULL;

  if (dialog->priv->icon_name == NULL || strlen (dialog->priv->icon_name) == 0)
    {
      image = ctk_image_new_from_icon_name ("dialog-password", CTK_ICON_SIZE_DIALOG);
      goto out;
    }

  vendor_pixbuf = ctk_icon_theme_load_icon (ctk_icon_theme_get_default (),
                                            dialog->priv->icon_name,
                                            48,
                                            0,
                                            NULL);
  if (vendor_pixbuf == NULL)
    {
      g_warning ("No icon for themed icon with name '%s'", dialog->priv->icon_name);
      image = ctk_image_new_from_icon_name ("dialog-password", CTK_ICON_SIZE_DIALOG);
      goto out;
    }


  pixbuf = ctk_icon_theme_load_icon (ctk_icon_theme_get_default (),
                                     "dialog-password",
                                     48,
                                     0,
                                     NULL);
  if (pixbuf == NULL)
    goto out;

  /* need to copy the pixbuf since we're modifying it */
  copy_pixbuf = gdk_pixbuf_copy (pixbuf);
  if (copy_pixbuf == NULL)
    goto out;

  /* blend the vendor icon in the bottom right quarter */
  gdk_pixbuf_composite (vendor_pixbuf,
                        copy_pixbuf,
                        24, 24, 24, 24,
                        24, 24, 0.5, 0.5,
                        GDK_INTERP_BILINEAR,
                        255);

  image = ctk_image_new_from_pixbuf (copy_pixbuf);

out:
  if (pixbuf != NULL)
    g_object_unref (pixbuf);

  if (copy_pixbuf != NULL)
    g_object_unref (copy_pixbuf);

  if (vendor_pixbuf != NULL)
    g_object_unref (vendor_pixbuf);

  return image;
}

static void
polkit_cafe_authentication_dialog_set_property (GObject      *object,
                                                 guint         prop_id,
                                                 const GValue *value,
                                                 GParamSpec   *pspec)
{
  PolkitCafeAuthenticationDialog *dialog = POLKIT_CAFE_AUTHENTICATION_DIALOG (object);

  switch (prop_id)
    {
    case PROP_DETAILS:
      dialog->priv->details = g_value_dup_object (value);
      break;

    case PROP_ACTION_ID:
      dialog->priv->action_id = g_value_dup_string (value);
      break;

    case PROP_VENDOR:
      dialog->priv->vendor = g_value_dup_string (value);
      break;

    case PROP_VENDOR_URL:
      dialog->priv->vendor_url = g_value_dup_string (value);
      break;

    case PROP_ICON_NAME:
      dialog->priv->icon_name = g_value_dup_string (value);
      break;

    case PROP_MESSAGE:
      dialog->priv->message = g_value_dup_string (value);
      break;

    case PROP_USERS:
      dialog->priv->users = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
polkit_cafe_authentication_dialog_get_property (GObject    *object,
                                                 guint       prop_id,
                                                 GValue     *value,
                                                 GParamSpec *pspec)
{
  PolkitCafeAuthenticationDialog *dialog = POLKIT_CAFE_AUTHENTICATION_DIALOG (object);

  switch (prop_id)
    {
    case PROP_MESSAGE:
      g_value_set_string (value, dialog->priv->message);
      break;

    /* TODO: rest of the properties */

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static CtkWidget *
add_row (CtkWidget *grid, int row, const char *label_text, CtkWidget *entry)
{
  CtkWidget *label;

  label = ctk_label_new_with_mnemonic (label_text);
  ctk_label_set_use_markup (CTK_LABEL (label), TRUE);
  ctk_label_set_xalign (CTK_LABEL (label), 1.0);
  ctk_label_set_yalign (CTK_LABEL (label), 0.5);

  ctk_widget_set_hexpand (label, FALSE);
  ctk_grid_attach (CTK_GRID (grid), label,
                   0, row, 1, 1);
  ctk_widget_set_hexpand (entry, TRUE);
  ctk_grid_attach (CTK_GRID (grid), entry,
                   1, row, 1, 1);
  ctk_label_set_mnemonic_widget (CTK_LABEL (label), entry);

  return label;
}

static void
action_id_activated (CtkLabel *url_label G_GNUC_UNUSED,
		     gpointer  user_data G_GNUC_UNUSED)
{
#if 0
  GError *error;
  DBusGConnection *bus;
  DBusGProxy *manager_proxy;

  error = NULL;
  bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (bus == NULL)
    {
      g_warning ("Couldn't connect to session bus: %s", error->message);
      g_error_free (error);
      goto out;
    }

  manager_proxy = dbus_g_proxy_new_for_name (bus,
                                             "org.cafe.PolicyKit.AuthorizationManager",
                                             "/",
                                             "org.cafe.PolicyKit.AuthorizationManager.SingleInstance");
  if (manager_proxy == NULL)
    {
      g_warning ("Could not construct manager_proxy object; bailing out");
      goto out;
    }

  if (!dbus_g_proxy_call (manager_proxy,
                          "ShowAction",
                          &error,
                          G_TYPE_STRING, ctk_label_get_current_uri (CTK_LABEL (url_label)),
                          G_TYPE_INVALID,
                          G_TYPE_INVALID))
    {
      if (error != NULL)
        {
          g_warning ("Failed to call into manager: %s", error->message);
          g_error_free (error);
        }
      else
        {
          g_warning ("Failed to call into manager");
        }
      goto out;
    }

out:
        ;
#endif
}

static void
polkit_cafe_authentication_dialog_init (PolkitCafeAuthenticationDialog *dialog)
{
  dialog->priv = polkit_cafe_authentication_dialog_get_instance_private (dialog);
}

static void
polkit_cafe_authentication_dialog_finalize (GObject *object)
{
  PolkitCafeAuthenticationDialog *dialog;

  dialog = POLKIT_CAFE_AUTHENTICATION_DIALOG (object);

  g_free (dialog->priv->message);
  g_free (dialog->priv->action_id);
  g_free (dialog->priv->vendor);
  g_free (dialog->priv->vendor_url);
  g_free (dialog->priv->icon_name);
  if (dialog->priv->details != NULL)
    g_object_unref (dialog->priv->details);

  g_strfreev (dialog->priv->users);
  g_free (dialog->priv->selected_user);

  if (dialog->priv->store != NULL)
    g_object_unref (dialog->priv->store);

  if (G_OBJECT_CLASS (polkit_cafe_authentication_dialog_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (polkit_cafe_authentication_dialog_parent_class)->finalize (object);
}

static CtkWidget*
polkit_cafe_dialog_add_button (CtkDialog   *dialog,
                               const gchar *button_text,
                               const gchar *icon_name,
                                     gint   response_id)
{
  CtkWidget *button;

  button = ctk_button_new_with_mnemonic (button_text);
  ctk_button_set_image (CTK_BUTTON (button), ctk_image_new_from_icon_name (icon_name, CTK_ICON_SIZE_BUTTON));

  ctk_button_set_use_underline (CTK_BUTTON (button), TRUE);
  ctk_style_context_add_class (ctk_widget_get_style_context (button), "text-button");
  ctk_widget_set_can_default (button, TRUE);
  ctk_widget_show (button);
  ctk_dialog_add_action_widget (CTK_DIALOG (dialog), button, response_id);

  return button;
}

static void
polkit_cafe_authentication_dialog_constructed (GObject *object)
{
  PolkitCafeAuthenticationDialog *dialog;
  CtkWidget *hbox;
  CtkWidget *main_vbox;
  CtkWidget *vbox;
  CtkWidget *grid_password;
  CtkWidget *details_expander;
  CtkWidget *details_vbox;
  CtkWidget *grid;
  CtkWidget *label;
  CtkWidget *image;
  CtkWidget *content_area;
  gboolean have_user_combobox;
  gchar *s;
  guint rows;

  dialog = POLKIT_CAFE_AUTHENTICATION_DIALOG (object);

  if (G_OBJECT_CLASS (polkit_cafe_authentication_dialog_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (polkit_cafe_authentication_dialog_parent_class)->constructed (object);

  have_user_combobox = FALSE;

  dialog->priv->cancel_button = polkit_cafe_dialog_add_button (CTK_DIALOG (dialog),
                                                               _("_Cancel"),
                                                               "process-stop",
                                                               CTK_RESPONSE_CANCEL);

  dialog->priv->auth_button = ctk_dialog_add_button (CTK_DIALOG (dialog),
                                                     _("_Authenticate"),
                                                     CTK_RESPONSE_OK);

  ctk_dialog_set_default_response (CTK_DIALOG (dialog), CTK_RESPONSE_OK);

  content_area = ctk_dialog_get_content_area (CTK_DIALOG (dialog));

  ctk_container_set_border_width (CTK_CONTAINER (dialog), 5);
  ctk_box_set_spacing (CTK_BOX (content_area), 2); /* 2 * 5 + 2 = 12 */
  ctk_window_set_resizable (CTK_WINDOW (dialog), FALSE);
  ctk_window_set_icon_name (CTK_WINDOW (dialog), "dialog-password");

  hbox = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 12);
  ctk_container_set_border_width (CTK_CONTAINER (hbox), 5);
  ctk_box_pack_start (CTK_BOX (content_area), hbox, TRUE, TRUE, 0);

  image = get_image (dialog);
  ctk_widget_set_halign (image, CTK_ALIGN_CENTER);
  ctk_widget_set_valign (image, CTK_ALIGN_START);
  ctk_box_pack_start (CTK_BOX (hbox), image, FALSE, FALSE, 0);

  main_vbox = ctk_box_new (CTK_ORIENTATION_VERTICAL, 10);
  ctk_box_pack_start (CTK_BOX (hbox), main_vbox, TRUE, TRUE, 0);

  /* main message */
  label = ctk_label_new (NULL);
  s = g_strdup_printf ("<big><b>%s</b></big>", dialog->priv->message);
  ctk_label_set_markup (CTK_LABEL (label), s);
  g_free (s);

  ctk_label_set_xalign (CTK_LABEL (label), 0.0);
  ctk_label_set_yalign (CTK_LABEL (label), 0.5);
  ctk_label_set_line_wrap (CTK_LABEL (label), TRUE);
  ctk_label_set_max_width_chars (CTK_LABEL (label), 50);
  ctk_box_pack_start (CTK_BOX (main_vbox), label, FALSE, FALSE, 0);

  /* secondary message */
  label = ctk_label_new (NULL);
  if (g_strv_length (dialog->priv->users) > 1)
    {
          ctk_label_set_markup (CTK_LABEL (label),
                                _("An application is attempting to perform an action that requires privileges. "
                                  "Authentication as one of the users below is required to perform this action."));
    }
  else
    {
      if (strcmp (g_get_user_name (), dialog->priv->users[0]) == 0)
        {
          ctk_label_set_markup (CTK_LABEL (label),
                                _("An application is attempting to perform an action that requires privileges. "
                                  "Authentication is required to perform this action."));
        }
      else
        {
          ctk_label_set_markup (CTK_LABEL (label),
                                _("An application is attempting to perform an action that requires privileges. "
                                  "Authentication as the super user is required to perform this action."));
        }
    }

  ctk_label_set_xalign (CTK_LABEL (label), 0.0);
  ctk_label_set_yalign (CTK_LABEL (label), 0.5);
  ctk_label_set_line_wrap (CTK_LABEL (label), TRUE);
  ctk_label_set_max_width_chars (CTK_LABEL (label), 50);
  ctk_box_pack_start (CTK_BOX (main_vbox), label, FALSE, FALSE, 0);

  /* user combobox */
  if (g_strv_length (dialog->priv->users) > 1)
    {
      dialog->priv->user_combobox = ctk_combo_box_new ();
      ctk_box_pack_start (CTK_BOX (main_vbox), CTK_WIDGET (dialog->priv->user_combobox), FALSE, FALSE, 0);

      create_user_combobox (dialog);

      have_user_combobox = TRUE;
    }
  else
    {
      dialog->priv->selected_user = g_strdup (dialog->priv->users[0]);
    }

  /* password entry */
  vbox = ctk_box_new (CTK_ORIENTATION_VERTICAL, 0);
  ctk_box_pack_start (CTK_BOX (main_vbox), vbox, FALSE, FALSE, 0);

  grid_password = ctk_grid_new ();
  ctk_grid_set_column_spacing (CTK_GRID (grid_password), 12);
  ctk_grid_set_row_spacing (CTK_GRID (grid_password), 6);
  ctk_box_pack_start (CTK_BOX (vbox), grid_password, FALSE, FALSE, 0);
  dialog->priv->password_entry = ctk_entry_new ();
  ctk_entry_set_visibility (CTK_ENTRY (dialog->priv->password_entry), FALSE);
  dialog->priv->prompt_label = add_row (grid_password, 0, _("_Password:"), dialog->priv->password_entry);

  g_signal_connect_swapped (dialog->priv->password_entry, "activate",
                            G_CALLBACK (ctk_window_activate_default),
                            dialog);

  dialog->priv->grid_password = grid_password;
  /* initially never show the password entry stuff; we'll toggle it on/off so it's
   * only shown when prompting for a password */
  ctk_widget_set_no_show_all (dialog->priv->grid_password, TRUE);

  /* A label for showing PAM_TEXT_INFO and PAM_TEXT_ERROR messages */
  label = ctk_label_new (NULL);
  ctk_label_set_line_wrap (CTK_LABEL (label), TRUE);
  ctk_box_pack_start (CTK_BOX (vbox), label, FALSE, FALSE, 0);
  dialog->priv->info_label = label;

  /* Details */
  details_expander = ctk_expander_new_with_mnemonic (_("<small><b>_Details</b></small>"));
  ctk_expander_set_use_markup (CTK_EXPANDER (details_expander), TRUE);
  ctk_box_pack_start (CTK_BOX (content_area), details_expander, FALSE, FALSE, 0);

  details_vbox = ctk_box_new (CTK_ORIENTATION_VERTICAL, 10);
  ctk_container_add (CTK_CONTAINER (details_expander), details_vbox);

  grid = ctk_grid_new ();
  ctk_widget_set_margin_start (grid, 20);
  ctk_grid_set_column_spacing (CTK_GRID (grid), 12);
  ctk_grid_set_row_spacing (CTK_GRID (grid), 6);
  ctk_box_pack_start (CTK_BOX (details_vbox), grid, FALSE, FALSE, 0);

  /* TODO: sort keys? */
  rows = 0;
  if (dialog->priv->details != NULL)
    {
      guint n;
      gchar **keys;

      keys = polkit_details_get_keys (dialog->priv->details);
      for (n = 0; keys[n] != NULL; n++)
        {
          const gchar *key = keys[n];
          const gchar *value;

          value = polkit_details_lookup (dialog->priv->details, key);

          label = ctk_label_new (NULL);
          s = g_strdup_printf ("<small>%s</small>", value);
          ctk_label_set_markup (CTK_LABEL (label), s);
          g_free (s);

          ctk_label_set_xalign (CTK_LABEL (label), 0.0);
          ctk_label_set_yalign (CTK_LABEL (label), 1.0);

          s = g_strdup_printf ("<small><b>%s:</b></small>", key);
          add_row (grid, rows, s, label);
          g_free (s);

          rows++;
        }
      g_strfreev (keys);
    }

  /* --- */

  label = ctk_label_new (NULL);
  ctk_label_set_use_markup (CTK_LABEL (label), TRUE);
  s = g_strdup_printf ("<small><a href=\"%s\">%s</a></small>",
                       dialog->priv->action_id,
                       dialog->priv->action_id);
  ctk_label_set_markup (CTK_LABEL (label), s);
  g_free (s);

  ctk_label_set_xalign (CTK_LABEL (label), 0.0);
  ctk_label_set_yalign (CTK_LABEL (label), 1.0);

  add_row (grid, rows++, _("<small><b>Action:</b></small>"), label);
  g_signal_connect (label, "activate-link", G_CALLBACK (action_id_activated), NULL);

  s = g_strdup_printf (_("Click to edit %s"), dialog->priv->action_id);
  ctk_widget_set_tooltip_markup (label, s);
  g_free (s);

  /* --- */

  label = ctk_label_new (NULL);
  ctk_label_set_use_markup (CTK_LABEL (label), TRUE);
  s = g_strdup_printf ("<small><a href=\"%s\">%s</a></small>",
                       dialog->priv->vendor_url,
                       dialog->priv->vendor);
  ctk_label_set_markup (CTK_LABEL (label), s);
  g_free (s);

  ctk_label_set_xalign (CTK_LABEL (label), 0.0);
  ctk_label_set_yalign (CTK_LABEL (label), 1.0);

  add_row (grid, rows++, _("<small><b>Vendor:</b></small>"), label);

  s = g_strdup_printf (_("Click to open %s"), dialog->priv->vendor_url);
  ctk_widget_set_tooltip_markup (label, s);
  g_free (s);

  /* Disable password entry and authenticate until have a user selected */
  if (have_user_combobox && ctk_combo_box_get_active (CTK_COMBO_BOX (dialog->priv->user_combobox)) == 0)
    {
      ctk_widget_set_sensitive (dialog->priv->prompt_label, FALSE);
      ctk_widget_set_sensitive (dialog->priv->password_entry, FALSE);
      ctk_widget_set_sensitive (dialog->priv->auth_button, FALSE);
    }

  ctk_widget_realize (CTK_WIDGET (dialog));

}

static void
polkit_cafe_authentication_dialog_class_init (PolkitCafeAuthenticationDialogClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = polkit_cafe_authentication_dialog_finalize;
  gobject_class->get_property = polkit_cafe_authentication_dialog_get_property;
  gobject_class->set_property = polkit_cafe_authentication_dialog_set_property;
  gobject_class->constructed  = polkit_cafe_authentication_dialog_constructed;

  g_object_class_install_property (gobject_class,
                                   PROP_DETAILS,
                                   g_param_spec_object ("details",
                                                        NULL,
                                                        NULL,
                                                        POLKIT_TYPE_DETAILS,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (gobject_class,
                                   PROP_ACTION_ID,
                                   g_param_spec_string ("action-id",
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (gobject_class,
                                   PROP_VENDOR,
                                   g_param_spec_string ("vendor",
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (gobject_class,
                                   PROP_VENDOR_URL,
                                   g_param_spec_string ("vendor-url",
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (gobject_class,
                                   PROP_ICON_NAME,
                                   g_param_spec_string ("icon-name",
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));


  g_object_class_install_property (gobject_class,
                                   PROP_MESSAGE,
                                   g_param_spec_string ("message",
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (gobject_class,
                                   PROP_USERS,
                                   g_param_spec_boxed ("users",
                                                       NULL,
                                                       NULL,
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_CONSTRUCT_ONLY |
                                                       G_PARAM_STATIC_NAME |
                                                       G_PARAM_STATIC_NICK |
                                                       G_PARAM_STATIC_BLURB));

  g_object_class_install_property (gobject_class,
                                   PROP_SELECTED_USER,
                                   g_param_spec_string ("selected-user",
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));
}

/**
 * polkit_cafe_authentication_dialog_new:
 *
 * Yada yada yada...
 *
 * Returns: A new password dialog.
 **/
CtkWidget *
polkit_cafe_authentication_dialog_new (const gchar    *action_id,
                                        const gchar    *vendor,
                                        const gchar    *vendor_url,
                                        const gchar    *icon_name,
                                        const gchar    *message_markup,
                                        PolkitDetails  *details,
                                        gchar         **users)
{
  PolkitCafeAuthenticationDialog *dialog;
  CtkWindow *window;

  dialog = g_object_new (POLKIT_CAFE_TYPE_AUTHENTICATION_DIALOG,
                         "action-id", action_id,
                         "vendor", vendor,
                         "vendor-url", vendor_url,
                         "icon-name", icon_name,
                         "message", message_markup,
                         "details", details,
                         "users", users,
                         NULL);

  window = CTK_WINDOW (dialog);

  ctk_window_set_position (window, CTK_WIN_POS_CENTER);
  ctk_window_set_modal (window, TRUE);
  ctk_window_set_resizable (window, FALSE);
  ctk_window_set_keep_above (window, TRUE);
  ctk_window_set_title (window, _("Authenticate"));
  g_signal_connect (dialog, "close", G_CALLBACK (ctk_widget_hide), NULL);

  return CTK_WIDGET (dialog);
}

/**
 * polkit_cafe_authentication_dialog_indicate_error:
 * @dialog: the auth dialog
 *
 * Call this function to indicate an authentication error; typically shakes the window
 **/
void
polkit_cafe_authentication_dialog_indicate_error (PolkitCafeAuthenticationDialog *dialog)
{
  int x, y;
  int n;
  int diff;

  /* TODO: detect compositing manager and do fancy stuff here */

  ctk_window_get_position (CTK_WINDOW (dialog), &x, &y);

  for (n = 0; n < 10; n++)
    {
      if (n % 2 == 0)
        diff = -15;
      else
        diff = 15;

      ctk_window_move (CTK_WINDOW (dialog), x + diff, y);

      while (ctk_events_pending ())
        {
          ctk_main_iteration ();
        }

      g_usleep (10000);
    }

  ctk_window_move (CTK_WINDOW (dialog), x, y);
}

/**
 * polkit_cafe_authentication_dialog_run_until_user_is_selected:
 * @dialog: A #PolkitCafeAuthenticationDialog.
 *
 * Runs @dialog in a recursive main loop until a user have been selected.
 *
 * If there is only one element in the the users array (which is set upon construction) or
 * an user has already been selected, this function returns immediately with the return
 * value %TRUE.
 *
 * Returns: %TRUE if a user is selected (use polkit_cafe_dialog_get_selected_user() to obtain the user) or
 *          %FALSE if the dialog was cancelled.
 **/
gboolean
polkit_cafe_authentication_dialog_run_until_user_is_selected (PolkitCafeAuthenticationDialog *dialog)
{
  gboolean ret;
  gint response;

  ret = FALSE;

  if (dialog->priv->selected_user != NULL)
    {
      ret = TRUE;
      goto out;
    }

  dialog->priv->is_running = TRUE;

  response = ctk_dialog_run (CTK_DIALOG (dialog));

  dialog->priv->is_running = FALSE;

  if (response == RESPONSE_USER_SELECTED)
    ret = TRUE;

 out:
  return ret;
}

/**
 * polkit_cafe_authentication_dialog_run_until_response_for_prompt:
 * @dialog: A #PolkitCafeAuthenticationDialog.
 * @prompt: The prompt to present the user with.
 * @echo_chars: Whether characters should be echoed in the password entry box.
 * @was_cancelled: Set to %TRUE if the dialog was cancelled.
 * @new_user_selected: Set to %TRUE if another user was selected.
 *
 * Runs @dialog in a recursive main loop until a response to @prompt have been obtained from the user.
 *
 * Returns: The response (free with g_free()) or %NULL if one of @was_cancelled or @new_user_selected
 *          has been set to %TRUE.
 **/
gchar *
polkit_cafe_authentication_dialog_run_until_response_for_prompt (PolkitCafeAuthenticationDialog *dialog,
                                                                  const gchar           *prompt,
                                                                  gboolean               echo_chars,
                                                                  gboolean              *was_cancelled,
                                                                  gboolean              *new_user_selected)
{
  gint response;
  gchar *ret;

  ctk_label_set_text_with_mnemonic (CTK_LABEL (dialog->priv->prompt_label), prompt);
  ctk_entry_set_visibility (CTK_ENTRY (dialog->priv->password_entry), echo_chars);
  ctk_entry_set_text (CTK_ENTRY (dialog->priv->password_entry), "");
  ctk_widget_grab_focus (dialog->priv->password_entry);

  ret = NULL;

  if (was_cancelled != NULL)
    *was_cancelled = FALSE;

  if (new_user_selected != NULL)
    *new_user_selected = FALSE;

  dialog->priv->is_running = TRUE;

  ctk_widget_set_no_show_all (dialog->priv->grid_password, FALSE);
  ctk_widget_show_all (dialog->priv->grid_password);

  response = ctk_dialog_run (CTK_DIALOG (dialog));

  ctk_widget_hide (dialog->priv->grid_password);
  ctk_widget_set_no_show_all (dialog->priv->grid_password, TRUE);

  dialog->priv->is_running = FALSE;

  if (response == CTK_RESPONSE_OK)
    {
      ret = g_strdup (ctk_entry_get_text (CTK_ENTRY (dialog->priv->password_entry)));
    }
  else if (response == RESPONSE_USER_SELECTED)
    {
      if (new_user_selected != NULL)
        *new_user_selected = TRUE;
    }
  else
    {
      if (was_cancelled != NULL)
        *was_cancelled = TRUE;
    }

  return ret;
}

/**
 * polkit_cafe_authentication_dialog_get_selected_user:
 * @dialog: A #PolkitCafeAuthenticationDialog.
 *
 * Gets the currently selected user.
 *
 * Returns: The currently selected user (free with g_free()) or %NULL if no user is currently selected.
 **/
gchar *
polkit_cafe_authentication_dialog_get_selected_user (PolkitCafeAuthenticationDialog *dialog)
{
  return g_strdup (dialog->priv->selected_user);
}

void
polkit_cafe_authentication_dialog_set_info_message (PolkitCafeAuthenticationDialog *dialog,
                                                     const gchar                     *info_markup)
{
  ctk_label_set_markup (CTK_LABEL (dialog->priv->info_label), info_markup);
}


/**
 * polkit_cafe_authentication_dialog_cancel:
 * @dialog: A #PolkitCafeAuthenticationDialog.
 *
 * Cancels the dialog if it is currenlty running.
 *
 * Returns: %TRUE if the dialog was running.
 **/
gboolean
polkit_cafe_authentication_dialog_cancel (PolkitCafeAuthenticationDialog *dialog)
{
  if (!dialog->priv->is_running)
    return FALSE;

  ctk_dialog_response (CTK_DIALOG (dialog), CTK_RESPONSE_CANCEL);

  return TRUE;
}
