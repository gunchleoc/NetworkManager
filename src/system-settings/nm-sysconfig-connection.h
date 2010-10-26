/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager system settings service
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2008 Novell, Inc.
 * (C) Copyright 2008 - 2010 Red Hat, Inc.
 */

#ifndef NM_SYSCONFIG_CONNECTION_H
#define NM_SYSCONFIG_CONNECTION_H

#include <nm-connection.h>
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

#define NM_TYPE_SYSCONFIG_CONNECTION            (nm_sysconfig_connection_get_type ())
#define NM_SYSCONFIG_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_SYSCONFIG_CONNECTION, NMSysconfigConnection))
#define NM_SYSCONFIG_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_SYSCONFIG_CONNECTION, NMSysconfigConnectionClass))
#define NM_IS_SYSCONFIG_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_SYSCONFIG_CONNECTION))
#define NM_IS_SYSCONFIG_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_SYSCONFIG_CONNECTION))
#define NM_SYSCONFIG_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_SYSCONFIG_CONNECTION, NMSysconfigConnectionClass))

#define NM_SYSCONFIG_CONNECTION_UPDATED "updated"
#define NM_SYSCONFIG_CONNECTION_REMOVED "removed"
#define NM_SYSCONFIG_CONNECTION_VISIBLE "visible"

typedef struct _NMSysconfigConnection NMSysconfigConnection;

typedef struct _NMSysconfigConnectionClass NMSysconfigConnectionClass;

typedef void (*NMSysconfigConnectionCommitFunc) (NMSysconfigConnection *connection,
                                                 GError *error,
                                                 gpointer user_data);

typedef void (*NMSysconfigConnectionDeleteFunc) (NMSysconfigConnection *connection,
                                                 GError *error,
                                                 gpointer user_data);

typedef void (*NMSysconfigConnectionGetSecretsFunc) (NMSysconfigConnection *connection,
                                                     GHashTable *secrets,
                                                     GError *error,
                                                     gpointer user_data);

struct _NMSysconfigConnection {
	NMConnection parent;
};

struct _NMSysconfigConnectionClass {
	NMConnectionClass parent;

	void (*commit_changes) (NMSysconfigConnection *connection,
	                        NMSysconfigConnectionCommitFunc callback,
	                        gpointer user_data);

	void (*delete) (NMSysconfigConnection *connection,
	                NMSysconfigConnectionDeleteFunc callback,
	                gpointer user_data);

	void (*get_secrets) (NMSysconfigConnection *connection,
	                     const char *setting_name,
	                     const char **hints,
	                     gboolean request_new,
	                     NMSysconfigConnectionGetSecretsFunc callback,
	                     gpointer user_data);
};

GType nm_sysconfig_connection_get_type (void);

void nm_sysconfig_connection_commit_changes (NMSysconfigConnection *connection,
                                             NMSysconfigConnectionCommitFunc callback,
                                             gpointer user_data);

gboolean nm_sysconfig_connection_replace_settings (NMSysconfigConnection *self,
                                                   NMConnection *new_settings,
                                                   GError **error);

void nm_sysconfig_connection_replace_and_commit (NMSysconfigConnection *self,
                                                 NMConnection *new_settings,
                                                 NMSysconfigConnectionCommitFunc callback,
                                                 gpointer user_data);

void nm_sysconfig_connection_delete (NMSysconfigConnection *connection,
                                     NMSysconfigConnectionDeleteFunc callback,
                                     gpointer user_data);

void nm_sysconfig_connection_get_secrets (NMSysconfigConnection *connection,
                                          const char *setting_name,
                                          const char **hints,
                                          gboolean request_new,
                                          NMSysconfigConnectionGetSecretsFunc callback,
                                          gpointer user_data);

gboolean nm_sysconfig_connection_is_visible (NMSysconfigConnection *self);

void nm_sysconfig_connection_recheck_visibility (NMSysconfigConnection *self);

G_END_DECLS

#endif /* NM_SYSCONFIG_CONNECTION_H */
