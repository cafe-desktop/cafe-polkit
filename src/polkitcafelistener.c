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


#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

#include "polkitcafelistener.h"
#include "polkitcafeauthenticator.h"

struct _PolkitCafeListener
{
  PolkitAgentListener parent_instance;

  /* we support multiple authenticators - they are simply queued up */
  GList *authenticators;

  PolkitCafeAuthenticator *active_authenticator;
};

struct _PolkitCafeListenerClass
{
  PolkitAgentListenerClass parent_class;
};

static void polkit_cafe_listener_initiate_authentication (PolkitAgentListener  *listener,
                                                           const gchar          *action_id,
                                                           const gchar          *message,
                                                           const gchar          *icon_name,
                                                           PolkitDetails        *details,
                                                           const gchar          *cookie,
                                                           GList                *identities,
                                                           GCancellable         *cancellable,
                                                           GAsyncReadyCallback   callback,
                                                           gpointer              user_data);

static gboolean polkit_cafe_listener_initiate_authentication_finish (PolkitAgentListener  *listener,
                                                                      GAsyncResult         *res,
                                                                      GError              **error);

G_DEFINE_TYPE (PolkitCafeListener, polkit_cafe_listener, POLKIT_AGENT_TYPE_LISTENER);

static void
polkit_cafe_listener_init (PolkitCafeListener *listener G_GNUC_UNUSED)
{
}

static void
polkit_cafe_listener_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (polkit_cafe_listener_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (polkit_cafe_listener_parent_class)->finalize (object);
}

static void
polkit_cafe_listener_class_init (PolkitCafeListenerClass *klass)
{
  GObjectClass *gobject_class;
  PolkitAgentListenerClass *listener_class;

  gobject_class = G_OBJECT_CLASS (klass);
  listener_class = POLKIT_AGENT_LISTENER_CLASS (klass);

  gobject_class->finalize = polkit_cafe_listener_finalize;

  listener_class->initiate_authentication          = polkit_cafe_listener_initiate_authentication;
  listener_class->initiate_authentication_finish   = polkit_cafe_listener_initiate_authentication_finish;
}

PolkitAgentListener *
polkit_cafe_listener_new (void)
{
  return POLKIT_AGENT_LISTENER (g_object_new (POLKIT_CAFE_TYPE_LISTENER, NULL));
}

typedef struct
{
  PolkitCafeListener *listener;
  PolkitCafeAuthenticator *authenticator;

  GTask        *task;
  GCancellable *cancellable;

  gulong cancel_id;
} AuthData;

static AuthData *
auth_data_new (PolkitCafeListener *listener,
               PolkitCafeAuthenticator *authenticator,
               GTask *task,
               GCancellable *cancellable)
{
  AuthData *data;

  data = g_new0 (AuthData, 1);
  data->listener = g_object_ref (listener);
  data->authenticator = g_object_ref (authenticator);
  data->task = g_object_ref (task);
  data->cancellable = g_object_ref (cancellable);
  return data;
}

static void
auth_data_free (AuthData *data)
{
  g_object_unref (data->listener);
  g_object_unref (data->authenticator);
  g_object_unref (data->task);
  if (data->cancellable != NULL && data->cancel_id > 0)
    g_signal_handler_disconnect (data->cancellable, data->cancel_id);
  g_object_unref (data->cancellable);
  g_free (data);
}

static void
maybe_initiate_next_authenticator (PolkitCafeListener *listener)
{
  if (listener->active_authenticator == NULL && listener->authenticators != NULL)
    {
      polkit_cafe_authenticator_initiate (POLKIT_CAFE_AUTHENTICATOR (listener->authenticators->data));
      listener->active_authenticator = listener->authenticators->data;
    }
}

static void
authenticator_completed (PolkitCafeAuthenticator *authenticator,
			 gboolean                 gained_authorization G_GNUC_UNUSED,
			 gboolean                 dismissed,
			 gpointer                 user_data)
{
  AuthData *data = user_data;

  data->listener->authenticators = g_list_remove (data->listener->authenticators, authenticator);
  if (authenticator == data->listener->active_authenticator)
    data->listener->active_authenticator = NULL;

  g_object_unref (authenticator);

  if (dismissed)
    {
      g_task_return_new_error (data->task,
                               POLKIT_ERROR,
                               POLKIT_ERROR_CANCELLED,
                               _("Authentication dialog was dismissed by the user"));
    }

  g_task_return_boolean (data->task, TRUE);
  g_object_unref (data->task);

  maybe_initiate_next_authenticator (data->listener);

  auth_data_free (data);
}

static void
cancelled_cb (GCancellable *cancellable G_GNUC_UNUSED,
	      gpointer      user_data)
{
  AuthData *data = user_data;

  polkit_cafe_authenticator_cancel (data->authenticator);
}

static void
polkit_cafe_listener_initiate_authentication (PolkitAgentListener  *agent_listener,
                                               const gchar          *action_id,
                                               const gchar          *message,
                                               const gchar          *icon_name,
                                               PolkitDetails        *details,
                                               const gchar          *cookie,
                                               GList                *identities,
                                               GCancellable         *cancellable,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data)
{
  PolkitCafeListener *listener = POLKIT_CAFE_LISTENER (agent_listener);
  GTask *task;
  PolkitCafeAuthenticator *authenticator;
  AuthData *data;

  task = g_task_new (G_OBJECT (listener),
                     NULL,
                     callback,
                     user_data);
  g_task_set_source_tag (task,
                         polkit_cafe_listener_initiate_authentication);

  authenticator = polkit_cafe_authenticator_new (action_id,
                                                  message,
                                                  icon_name,
                                                  details,
                                                  cookie,
                                                  identities);
  if (authenticator == NULL)
    {
      g_task_return_new_error (task,
                               POLKIT_ERROR,
                               POLKIT_ERROR_FAILED,
                               "Error creating authentication object");
      g_object_unref (task);
      goto out;
    }

  data = auth_data_new (listener, authenticator, task, cancellable);

  g_signal_connect (authenticator,
                    "completed",
                    G_CALLBACK (authenticator_completed),
                    data);

  if (cancellable != NULL)
    {
      data->cancel_id = g_signal_connect (cancellable,
                                          "cancelled",
                                          G_CALLBACK (cancelled_cb),
                                          data);
    }

  listener->authenticators = g_list_append (listener->authenticators, authenticator);

  maybe_initiate_next_authenticator (listener);

 out:
  ;
}

static gboolean
polkit_cafe_listener_initiate_authentication_finish (PolkitAgentListener  *listener G_GNUC_UNUSED,
						     GAsyncResult         *res,
						     GError              **error)
{
  GTask *task = G_TASK (res);

  g_warn_if_fail (g_task_get_source_tag (task) == polkit_cafe_listener_initiate_authentication);

  if (g_task_propagate_pointer (task, error) == NULL) {
    return FALSE;
  }

  return TRUE;
}

