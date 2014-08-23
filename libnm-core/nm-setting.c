/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

/*
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright 2007 - 2011 Red Hat, Inc.
 * Copyright 2007 - 2008 Novell, Inc.
 */

#include <string.h>
#include <glib/gi18n.h>

#include "nm-setting.h"
#include "nm-setting-private.h"
#include "nm-setting-connection.h"
#include "nm-utils.h"
#include "nm-utils-private.h"
#include "nm-property-compare.h"

/**
 * SECTION:nm-setting
 * @short_description: Describes related configuration information
 *
 * Each #NMSetting contains properties that describe configuration that applies
 * to a specific network layer (like IPv4 or IPv6 configuration) or device type
 * (like Ethernet, or Wi-Fi).  A collection of individual settings together
 * make up an #NMConnection. Each property is strongly typed and usually has
 * a number of allowed values.  See each #NMSetting subclass for a description
 * of properties and allowed values.
 */

/**
 * nm_setting_error_quark:
 *
 * Registers an error quark for #NMSetting if necessary.
 *
 * Returns: the error quark used for NMSetting errors.
 **/
GQuark
nm_setting_error_quark (void)
{
	static GQuark quark;

	if (G_UNLIKELY (!quark))
		quark = g_quark_from_static_string ("nm-setting-error-quark");
	return quark;
}

G_DEFINE_ABSTRACT_TYPE (NMSetting, nm_setting, G_TYPE_OBJECT)

#define NM_SETTING_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SETTING, NMSettingPrivate))

typedef struct {
	const char *name;
	GType type;
	guint32 priority;
	GQuark error_quark;
} SettingInfo;

typedef struct {
	const SettingInfo *info;
} NMSettingPrivate;

enum {
	PROP_0,
	PROP_NAME,

	PROP_LAST
};

/*************************************************************/

static GHashTable *registered_settings = NULL;
static GHashTable *registered_settings_by_type = NULL;

static gboolean
_nm_gtype_equal (gconstpointer v1, gconstpointer v2)
{
	return *((const GType *) v1) == *((const GType *) v2);
}
static guint
_nm_gtype_hash (gconstpointer v)
{
	return *((const GType *) v);
}

static void __attribute__((constructor))
_ensure_registered (void)
{
	if (G_UNLIKELY (registered_settings == NULL)) {
#if !GLIB_CHECK_VERSION (2, 35, 0)
		g_type_init ();
#endif
		_nm_value_transforms_register ();
		registered_settings = g_hash_table_new (g_str_hash, g_str_equal);
		registered_settings_by_type = g_hash_table_new (_nm_gtype_hash, _nm_gtype_equal);
	}
}

#define _ensure_setting_info(self, priv) \
	G_STMT_START { \
		NMSettingPrivate *_priv_esi = (priv); \
		if (G_UNLIKELY (!_priv_esi->info)) { \
			_priv_esi->info = _nm_setting_lookup_setting_by_type (G_OBJECT_TYPE (self)); \
			g_assert (_priv_esi->info); \
		} \
	} G_STMT_END

/*************************************************************/

/*
 * _nm_register_setting:
 * @name: the name of the #NMSetting object to register
 * @type: the #GType of the #NMSetting
 * @priority: the sort priority of the setting, see below
 * @error_quark: the setting's error quark
 *
 * INTERNAL ONLY: registers a setting's internal properties, like its priority
 * and its error quark type, with libnm.
 *
 * A setting's priority should roughly follow the OSI layer model, but it also
 * controls which settings get asked for secrets first.  Thus settings which
 * relate to things that must be working first, like hardware, should get a
 * higher priority than things which layer on top of the hardware.  For example,
 * the GSM/CDMA settings should provide secrets before the PPP setting does,
 * because a PIN is required to unlock the device before PPP can even start.
 * Even settings without secrets should be assigned the right priority.
 *
 * 0: reserved for the Connection setting
 *
 * 1: hardware-related settings like Ethernet, Wi-Fi, InfiniBand, Bridge, etc.
 * These priority 1 settings are also "base types", which means that at least
 * one of them is required for the connection to be valid, and their name is
 * valid in the 'type' property of the Connection setting.
 *
 * 2: hardware-related auxiliary settings that require a base setting to be
 * successful first, like Wi-Fi security, 802.1x, etc.
 *
 * 3: hardware-independent settings that are required before IP connectivity
 * can be established, like PPP, PPPoE, etc.
 *
 * 4: IP-level stuff
 */
void
(_nm_register_setting) (const char *name,
                        const GType type,
                        const guint32 priority,
                        const GQuark error_quark)
{
	SettingInfo *info;

	g_return_if_fail (name != NULL && *name);
	g_return_if_fail (type != G_TYPE_INVALID);
	g_return_if_fail (type != G_TYPE_NONE);
	g_return_if_fail (error_quark != 0);
	g_return_if_fail (priority <= 4);

	_ensure_registered ();

	if (G_LIKELY ((info = g_hash_table_lookup (registered_settings, name)))) {
		g_return_if_fail (info->type == type);
		g_return_if_fail (info->error_quark == error_quark);
		g_return_if_fail (info->priority == priority);
		g_return_if_fail (g_strcmp0 (info->name, name) == 0);
		return;
	}
	g_return_if_fail (g_hash_table_lookup (registered_settings_by_type, &type) == NULL);

	if (priority == 0)
		g_assert_cmpstr (name, ==, NM_SETTING_CONNECTION_SETTING_NAME);

	info = g_slice_new0 (SettingInfo);
	info->type = type;
	info->priority = priority;
	info->error_quark = error_quark;
	info->name = name;
	g_hash_table_insert (registered_settings, (void *) info->name, info);
	g_hash_table_insert (registered_settings_by_type, &info->type, info);
}

static const SettingInfo *
_nm_setting_lookup_setting_by_type (GType type)
{
	_ensure_registered ();
	return g_hash_table_lookup (registered_settings_by_type, &type);
}

static guint32
_get_setting_type_priority (GType type)
{
	const SettingInfo *info;

	g_return_val_if_fail (g_type_is_a (type, NM_TYPE_SETTING), G_MAXUINT32);

	info = _nm_setting_lookup_setting_by_type (type);
	return info->priority;
}

gboolean
_nm_setting_type_is_base_type (GType type)
{
	/* Historical oddity: PPPoE is a base-type even though it's not
	 * priority 1.  It needs to be sorted *after* lower-level stuff like
	 * Wi-Fi security or 802.1x for secrets, but it's still allowed as a
	 * base type.
	 */
	return _get_setting_type_priority (type) == 1 || (type == NM_TYPE_SETTING_PPPOE);
}

gboolean
_nm_setting_is_base_type (NMSetting *setting)
{
	return _nm_setting_type_is_base_type (G_OBJECT_TYPE (setting));
}

/**
 * nm_setting_lookup_type:
 * @name: a setting name
 *
 * Returns the #GType of the setting's class for a given setting name.
 *
 * Returns: the #GType of the setting's class, or %G_TYPE_INVALID if
 *   @name is not recognized.
 **/
GType
nm_setting_lookup_type (const char *name)
{
	SettingInfo *info;

	g_return_val_if_fail (name != NULL, G_TYPE_INVALID);

	_ensure_registered ();

	info = g_hash_table_lookup (registered_settings, name);
	return info ? info->type : G_TYPE_INVALID;
}

/**
 * nm_setting_lookup_type_by_quark:
 * @error_quark: a setting error quark
 *
 * Returns the #GType of the setting's class for a given setting error quark.
 * Useful for figuring out which setting a returned error is for.
 *
 * Returns: the #GType of the setting's class, or %G_TYPE_INVALID if
 *   @error_quark is not recognized
 **/
GType
nm_setting_lookup_type_by_quark (GQuark error_quark)
{
	SettingInfo *info;
	GHashTableIter iter;

	_ensure_registered ();

	g_hash_table_iter_init (&iter, registered_settings);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer) &info)) {
		if (info->error_quark == error_quark)
			return info->type;
	}
	return G_TYPE_INVALID;
}

static GQuark
_nm_setting_lookup_error_quark (const char *name)
{
	SettingInfo *info;

	g_return_val_if_fail (name != NULL, 0);

	_ensure_registered ();

	info = g_hash_table_lookup (registered_settings, name);
	return info ? info->error_quark : 0;
}

gint
_nm_setting_compare_priority (gconstpointer a, gconstpointer b)
{
	guint32 prio_a, prio_b;

	prio_a = _get_setting_type_priority (G_OBJECT_TYPE (a));
	prio_b = _get_setting_type_priority (G_OBJECT_TYPE (b));

	if (prio_a < prio_b)
		return -1;
	else if (prio_a == prio_b)
		return 0;
	return 1;
}

/*************************************************************/

gboolean
_nm_setting_slave_type_is_valid (const char *slave_type, const char **out_port_type)
{
	const char *port_type = NULL;
	gboolean found = TRUE;

	if (!slave_type)
		found = FALSE;
	else if (!strcmp (slave_type, NM_SETTING_BOND_SETTING_NAME))
		;
	else if (!strcmp (slave_type, NM_SETTING_BRIDGE_SETTING_NAME))
		port_type = NM_SETTING_BRIDGE_PORT_SETTING_NAME;
	else if (!strcmp (slave_type, NM_SETTING_TEAM_SETTING_NAME))
		port_type = NM_SETTING_TEAM_PORT_SETTING_NAME;
	else
		found = FALSE;

	if (out_port_type)
		*out_port_type = port_type;
	return found;
}


NMSetting *
_nm_setting_find_in_list_base_type (GSList *all_settings)
{
	GSList *iter;
	NMSetting *setting = NULL;

	for (iter = all_settings; iter; iter = iter->next) {
		NMSetting *s_iter = NM_SETTING (iter->data);

		if (!_nm_setting_is_base_type (s_iter))
			continue;

		if (setting) {
			/* FIXME: currently, if there is more than one matching base type,
			 * we cannot detect the base setting.
			 * See: https://bugzilla.gnome.org/show_bug.cgi?id=696936#c8 */
			return NULL;
		}
		setting = s_iter;
	}
	return setting;
}

const char *
_nm_setting_slave_type_detect_from_settings (GSList *all_settings, NMSetting **out_s_port)
{
	GSList *iter;
	const char *slave_type = NULL;
	NMSetting *s_port = NULL;

	for (iter = all_settings; iter; iter = iter->next) {
		NMSetting *s_iter = NM_SETTING (iter->data);
		const char *name = nm_setting_get_name (s_iter);
		const char *i_slave_type = NULL;

		if (!strcmp (name, NM_SETTING_BRIDGE_PORT_SETTING_NAME))
			i_slave_type = NM_SETTING_BRIDGE_SETTING_NAME;
		else if (!strcmp (name, NM_SETTING_TEAM_PORT_SETTING_NAME))
			i_slave_type = NM_SETTING_TEAM_SETTING_NAME;
		else
			continue;

		if (slave_type) {
			/* there are more then one matching port types, cannot detect the slave type. */
			slave_type = NULL;
			s_port = NULL;
			break;
		}
		slave_type = i_slave_type;
		s_port = s_iter;
	}

	if (out_s_port)
		*out_s_port = s_port;
	return slave_type;
}

/*************************************************************/

typedef struct {
	const char *name;
	GParamSpec *param_spec;
	GType dbus_type;

	NMSettingPropertyGetFunc get_func;
	NMSettingPropertySetFunc set_func;
	NMSettingPropertyNotSetFunc not_set_func;

	NMSettingPropertyTransformFunc to_dbus;
	NMSettingPropertyTransformFunc from_dbus;
} NMSettingProperty;

static GQuark setting_property_overrides_quark;
static GQuark setting_properties_quark;

static NMSettingProperty *
find_property (GArray *properties, const char *name)
{
	NMSettingProperty *property;
	int i;

	if (!properties)
		return NULL;

	for (i = 0; i < properties->len; i++) {
		property = &g_array_index (properties, NMSettingProperty, i);
		if (strcmp (name, property->name) == 0)
			return property;
	}

	return NULL;
}

static void
add_property_override (NMSettingClass *setting_class,
                       const char *property_name,
                       GParamSpec *param_spec,
                       GType dbus_type,
                       NMSettingPropertyGetFunc get_func,
                       NMSettingPropertySetFunc set_func,
                       NMSettingPropertyNotSetFunc not_set_func,
                       NMSettingPropertyTransformFunc to_dbus,
                       NMSettingPropertyTransformFunc from_dbus)
{
	GType setting_type = G_TYPE_FROM_CLASS (setting_class);
	GArray *overrides;
	NMSettingProperty override;

	g_return_if_fail (g_type_get_qdata (setting_type, setting_properties_quark) == NULL);

	memset (&override, 0, sizeof (override));
	override.name = property_name;
	override.param_spec = param_spec;
	override.dbus_type = dbus_type;
	override.get_func = get_func;
	override.set_func = set_func;
	override.not_set_func = not_set_func;
	override.to_dbus = to_dbus;
	override.from_dbus = from_dbus;

	overrides = g_type_get_qdata (setting_type, setting_property_overrides_quark);
	if (!overrides) {
		overrides = g_array_new (FALSE, FALSE, sizeof (NMSettingProperty));
		g_type_set_qdata (setting_type, setting_property_overrides_quark, overrides);
	}
	g_return_if_fail (find_property (overrides, property_name) == NULL);

	g_array_append_val (overrides, override);
}

/**
 * _nm_setting_class_add_dbus_only_property:
 * @setting_class: the setting class
 * @property_name: the name of the property to override
 * @dbus_type: the type of the property
 * @get_func: (allow-none): function to call to get the value of the property
 * @set_func: (allow-none): function to call to set the value of the property
 *
 * Registers a property named @property_name, which will be used in the D-Bus
 * serialization of objects of @setting_class, but which does not correspond to
 * a #GObject property.
 *
 * When serializing a setting to D-Bus, @get_func will be called to get the
 * property's value. If it returns %TRUE, the value will be added to the hash,
 * and if %FALSE, it will not. (If @get_func is %NULL, the property will always
 * be omitted in the serialization.)
 *
 * When deserializing a D-Bus representation into a setting, if @property_name
 * is present, then @set_func will be called to set (and/or verify) it. If it
 * returns %TRUE, the value is considered to have been successfully set; if it
 * returns %FALSE then the deserializing operation as a whole will fail with the
 * returned #GError. (If @set_func is %NULL then the property will be ignored
 * when deserializing.)
 */
void
_nm_setting_class_add_dbus_only_property (NMSettingClass *setting_class,
                                          const char *property_name,
                                          GType dbus_type,
                                          NMSettingPropertyGetFunc get_func,
                                          NMSettingPropertySetFunc set_func)
{
	g_return_if_fail (NM_IS_SETTING_CLASS (setting_class));
	g_return_if_fail (property_name != NULL);

	/* Must not match any GObject property. */
	g_return_if_fail (!g_object_class_find_property (G_OBJECT_CLASS (setting_class), property_name));

	add_property_override (setting_class,
	                       property_name, NULL, dbus_type,
	                       get_func, set_func, NULL,
	                       NULL, NULL);
}

/**
 * _nm_setting_class_override_property:
 * @setting_class: the setting class
 * @property_name: the name of the property to override
 * @dbus_type: the type of the property (in its D-Bus representation)
 * @get_func: (allow-none): function to call to get the value of the property
 * @set_func: (allow-none): function to call to set the value of the property
 * @not_set_func: (allow-none): function to call to indicate the property was not set
 *
 * Overrides the D-Bus representation of the #GObject property named
 * @property_name on @setting_class.
 *
 * When serializing a setting to D-Bus, if @get_func is non-%NULL, then it will
 * be called to get the property's value. If it returns %TRUE, the value will be
 * added to the hash, and if %FALSE, it will not. (If @get_func is %NULL, the
 * property will be read normally with g_object_get_property(), and added to the
 * hash if it is not the default value.)
 *
 * When deserializing a D-Bus representation into a setting, if @property_name
 * is present, then @set_func will be called to set (and/or verify) it. If it
 * returns %TRUE, the value is considered to have been successfully set; if it
 * returns %FALSE then the deserializing operation as a whole will fail with the
 * returned #GError. (If @set_func is %NULL then the property will be set normally
 * with g_object_set_property().)
 *
 * If @not_set_func is non-%NULL, then it will be called when deserializing a
 * representation that does NOT contain @property_name. This can be used, eg, if
 * a new property needs to be initialized from some older deprecated property
 * when it is not present.
 */
void
_nm_setting_class_override_property (NMSettingClass *setting_class,
                                     const char *property_name,
                                     GType dbus_type,
                                     NMSettingPropertyGetFunc get_func,
                                     NMSettingPropertySetFunc set_func,
                                     NMSettingPropertyNotSetFunc not_set_func)
{
	GParamSpec *param_spec;

	param_spec = g_object_class_find_property (G_OBJECT_CLASS (setting_class), property_name);
	g_return_if_fail (param_spec != NULL);

	add_property_override (setting_class,
	                       property_name, param_spec, dbus_type,
	                       get_func, set_func, not_set_func,
	                       NULL, NULL);
}

/**
 * _nm_setting_class_transform_property:
 * @setting_class: the setting class
 * @property: the name of the property to transform
 * @dbus_type: the type of the property (in its D-Bus representation)
 * @to_dbus: function to convert from object to D-Bus format
 * @from_dbus: function to convert from D-Bus to object format
 *
 * Indicates that @property on @setting_class does not have the same format as
 * its corresponding D-Bus representation, and so must be transformed when
 * serializing/deserializing.
 *
 * The transformation will also be used by nm_setting_compare(), meaning that
 * the underlying object property does not need to be of a type that
 * nm_property_compare() recognizes, as long as it recognizes @dbus_type.
 */
void
_nm_setting_class_transform_property (NMSettingClass *setting_class,
                                      const char *property,
                                      GType dbus_type,
                                      NMSettingPropertyTransformFunc to_dbus,
                                      NMSettingPropertyTransformFunc from_dbus)
{
	GParamSpec *param_spec;

	param_spec = g_object_class_find_property (G_OBJECT_CLASS (setting_class), property);
	g_return_if_fail (param_spec != NULL);

	add_property_override (setting_class,
	                       property, param_spec, dbus_type,
	                       NULL, NULL, NULL,
	                       to_dbus, from_dbus);
}

static GArray *
nm_setting_class_ensure_properties (NMSettingClass *setting_class)
{
	GType type = G_TYPE_FROM_CLASS (setting_class), otype;
	NMSettingProperty property, *override;
	GArray *overrides, *type_overrides, *properties;
	GParamSpec **property_specs;
	guint n_property_specs, i;

	properties = g_type_get_qdata (type, setting_properties_quark);
	if (properties)
		return properties;

	/* Build overrides array from @setting_class and its superclasses */
	overrides = g_array_new (FALSE, FALSE, sizeof (NMSettingProperty));
	for (otype = type; otype != G_TYPE_OBJECT; otype = g_type_parent (otype)) {
		type_overrides = g_type_get_qdata (otype, setting_property_overrides_quark);
		if (type_overrides)
			g_array_append_vals (overrides, (NMSettingProperty *)type_overrides->data, type_overrides->len);
	}

	/* Build the properties array from the GParamSpecs, obeying overrides */
	properties = g_array_new (FALSE, FALSE, sizeof (NMSettingProperty));

	property_specs = g_object_class_list_properties (G_OBJECT_CLASS (setting_class),
	                                                 &n_property_specs);
	for (i = 0; i < n_property_specs; i++) {
		override = find_property (overrides, property_specs[i]->name);
		if (override)
			property = *override;
		else {
			memset (&property, 0, sizeof (property));
			property.name = property_specs[i]->name;
			property.param_spec = property_specs[i];
		}
		g_array_append_val (properties, property);
	}
	g_free (property_specs);

	/* Add any remaining overrides not corresponding to GObject properties */
	for (i = 0; i < overrides->len; i++) {
		override = &g_array_index (overrides, NMSettingProperty, i);
		if (!g_object_class_find_property (G_OBJECT_CLASS (setting_class), override->name))
			g_array_append_val (properties, *override);
	}
	g_array_unref (overrides);

	g_type_set_qdata (type, setting_properties_quark, properties);
	return properties;
}

static const NMSettingProperty *
nm_setting_class_get_properties (NMSettingClass *setting_class, guint *n_properties)
{
	GArray *properties;

	properties = nm_setting_class_ensure_properties (setting_class);

	*n_properties = properties->len;
	return (NMSettingProperty *) properties->data;
}

static const NMSettingProperty *
nm_setting_class_find_property (NMSettingClass *setting_class, const char *property_name)
{
	GArray *properties;

	properties = nm_setting_class_ensure_properties (setting_class);
	return find_property (properties, property_name);
}

/*************************************************************/

static void
destroy_gvalue (gpointer data)
{
	GValue *value = (GValue *) data;

	if (G_IS_VALUE (value))
		g_value_unset (value);
	g_slice_free (GValue, value);
}

/**
 * _nm_setting_to_dbus:
 * @setting: the #NMSetting
 * @connection: the #NMConnection containing @setting
 * @flags: hash flags, e.g. %NM_CONNECTION_SERIALIZE_ALL
 *
 * Converts the #NMSetting into a #GHashTable mapping each setting property
 * name to a GValue describing that property, suitable for marshalling over
 * D-Bus or serializing.  The mapping is string to GValue.
 *
 * Returns: (transfer full) (element-type utf8 GObject.Value): a new #GHashTable
 * describing the setting's properties
 **/
GHashTable *
_nm_setting_to_dbus (NMSetting *setting, NMConnection *connection, NMConnectionSerializationFlags flags)
{
	GHashTable *hash;
	const NMSettingProperty *properties;
	guint n_properties, i;

	g_return_val_if_fail (NM_IS_SETTING (setting), NULL);

	properties = nm_setting_class_get_properties (NM_SETTING_GET_CLASS (setting), &n_properties);

	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
	                              (GDestroyNotify) g_free, destroy_gvalue);

	for (i = 0; i < n_properties; i++) {
		const NMSettingProperty *property = &properties[i];
		GParamSpec *prop_spec = property->param_spec;
		GValue *value;
		gboolean set;

		if (!prop_spec && !property->get_func) {
			/* Override property with no get_func, so we skip it. */
			continue;
		}

		if (prop_spec && !(prop_spec->flags & G_PARAM_WRITABLE))
			continue;

		if (   (flags & NM_CONNECTION_SERIALIZE_NO_SECRETS)
		    && (prop_spec && (prop_spec->flags & NM_SETTING_PARAM_SECRET)))
			continue;

		if (   (flags & NM_CONNECTION_SERIALIZE_ONLY_SECRETS)
		    && !(prop_spec && (prop_spec->flags & NM_SETTING_PARAM_SECRET)))
			continue;

		value = g_slice_new0 (GValue);
		if (property->get_func) {
			g_value_init (value, property->dbus_type);
			set = property->get_func (setting, connection, property->name, value);
		} else if (prop_spec) {
			g_value_init (value, prop_spec->value_type);
			g_object_get_property (G_OBJECT (setting), prop_spec->name, value);

			/* Don't serialize values with default values */
			set = !g_param_value_defaults (prop_spec, value);

			/* Convert the property value if necessary */
			if (set && property->to_dbus) {
				GValue *dbus_value = g_slice_new0 (GValue);

				g_value_init (dbus_value, property->dbus_type);
				property->to_dbus (value, dbus_value);
				destroy_gvalue (value);
				value = dbus_value;
			}
		} else
			g_assert_not_reached ();

		if (set)
			g_hash_table_insert (hash, g_strdup (property->name), value);
		else
			destroy_gvalue (value);
	}

	return hash;
}

/**
 * _nm_setting_new_from_dbus:
 * @setting_type: the #NMSetting type which the hash contains properties for
 * @setting_hash: (element-type utf8 GObject.Value): the #GHashTable containing a
 *   string to #GValue mapping of properties that apply to the setting
 * @connection_hash: (element-type utf8 GObject.Value): the #GHashTable containing a
 *   string to #GHashTable mapping of properties for the whole connection
 * @error: location to store error, or %NULL
 *
 * Creates a new #NMSetting object and populates that object with the properties
 * contained in the hash table, using each hash key as the property to set,
 * and each hash value as the value to set that property to.  Setting properties
 * are strongly typed, thus the GValue type of the hash value must be correct.
 * See the documentation on each #NMSetting object subclass for the correct
 * property names and value types.
 *
 * Returns: a new #NMSetting object populated with the properties from the
 * hash table, or %NULL on failure
 **/
NMSetting *
_nm_setting_new_from_dbus (GType setting_type,
                           GHashTable *setting_hash,
                           GHashTable *connection_hash,
                           GError **error)
{
	NMSetting *setting;
	NMSettingClass *class;
	GHashTableIter iter;
	const char *prop_name;
	const NMSettingProperty *properties;
	guint n_properties;
	guint i;

	g_return_val_if_fail (G_TYPE_IS_INSTANTIATABLE (setting_type), NULL);
	g_return_val_if_fail (setting_hash != NULL, NULL);

	/* g_type_class_ref() ensures the setting class is created if it hasn't
	 * already been used.
	 */
	class = g_type_class_ref (setting_type);

	/* Check for invalid properties first. */
	g_hash_table_iter_init (&iter, setting_hash);
	while (g_hash_table_iter_next (&iter, (gpointer) &prop_name, NULL)) {
		if (!nm_setting_class_find_property (class, prop_name)) {
			/* Oh, we're so nice and only warn, maybe it should be a fatal error? */
			g_warning ("Ignoring invalid property '%s'", prop_name);
			continue;
		}
	}

	/* Now build the setting object from the legitimate properties */
	setting = (NMSetting *) g_object_new (setting_type, NULL);

	properties = nm_setting_class_get_properties (class, &n_properties);
	for (i = 0; i < n_properties; i++) {
		const NMSettingProperty *property = &properties[i];
		GValue *value = g_hash_table_lookup (setting_hash, property->name);

		if (value && property->set_func) {
			if (!property->set_func (setting,
			                         connection_hash,
			                         property->name,
			                         value,
			                         error)) {
				g_object_unref (setting);
				setting = NULL;
				break;
			}
		} else if (!value && property->not_set_func) {
			if (!property->not_set_func (setting,
			                             connection_hash,
			                             property->name,
			                             error)) {
				g_object_unref (setting);
				setting = NULL;
				break;
			}
		} else if (value && property->param_spec) {
			if (!(property->param_spec->flags & G_PARAM_WRITABLE))
				continue;

			if (property->from_dbus)  {
				GValue object_value = G_VALUE_INIT;

				g_value_init (&object_value, property->param_spec->value_type);
				property->from_dbus (value, &object_value);
				g_object_set_property (G_OBJECT (setting), property->param_spec->name, &object_value);
				g_value_unset (&object_value);
			} else
				g_object_set_property (G_OBJECT (setting), property->param_spec->name, value);
		}
	}

	g_type_class_unref (class);

	return setting;
}

static void
duplicate_setting (NMSetting *setting,
                   const char *name,
                   const GValue *value,
                   GParamFlags flags,
                   gpointer user_data)
{
	if ((flags & (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY)) == G_PARAM_WRITABLE)
		g_object_set_property (G_OBJECT (user_data), name, value);
}

/**
 * nm_setting_duplicate:
 * @setting: the #NMSetting to duplicate
 *
 * Duplicates a #NMSetting.
 *
 * Returns: (transfer full): a new #NMSetting containing the same properties and values as the
 * source #NMSetting
 **/
NMSetting *
nm_setting_duplicate (NMSetting *setting)
{
	GObject *dup;

	g_return_val_if_fail (NM_IS_SETTING (setting), NULL);

	dup = g_object_new (G_OBJECT_TYPE (setting), NULL);

	g_object_freeze_notify (dup);
	nm_setting_enumerate_values (setting, duplicate_setting, dup);
	g_object_thaw_notify (dup);

	return NM_SETTING (dup);
}

static gint
find_setting_by_name (gconstpointer a, gconstpointer b)
{
	NMSetting *setting = NM_SETTING (a);
	const char *str = (const char *) b;

	return strcmp (nm_setting_get_name (setting), str);
}

NMSetting *
nm_setting_find_in_list (GSList     *settings_list,
                         const char *setting_name)
{
	GSList *found;

	found = g_slist_find_custom (settings_list, setting_name, find_setting_by_name);
	if (found)
		return found->data;
	else
		return NULL;
}

/**
 * nm_setting_get_name:
 * @setting: the #NMSetting
 *
 * Returns the type name of the #NMSetting object
 *
 * Returns: a string containing the type name of the #NMSetting object,
 * like 'ppp' or 'wireless' or 'wired'.
 **/
const char *
nm_setting_get_name (NMSetting *setting)
{
	NMSettingPrivate *priv;

	g_return_val_if_fail (NM_IS_SETTING (setting), NULL);
	priv = NM_SETTING_GET_PRIVATE (setting);
	_ensure_setting_info (setting, priv);
	return priv->info->name;
}

/**
 * nm_setting_verify:
 * @setting: the #NMSetting to verify
 * @all_settings: (element-type NMSetting): a #GSList of all settings
 *     in the connection from which @setting came
 * @error: location to store error, or %NULL
 *
 * Validates the setting.  Each setting's properties have allowed values, and
 * some are dependent on other values (hence the need for @all_settings).  The
 * returned #GError contains information about which property of the setting
 * failed validation, and in what way that property failed validation.
 *
 * Returns: %TRUE if the setting is valid, %FALSE if it is not
 **/
gboolean
nm_setting_verify (NMSetting *setting, GSList *all_settings, GError **error)
{
	NMSettingVerifyResult result = _nm_setting_verify (setting, all_settings, error);

	if (result == NM_SETTING_VERIFY_NORMALIZABLE)
		g_clear_error (error);

	return result == NM_SETTING_VERIFY_SUCCESS || result == NM_SETTING_VERIFY_NORMALIZABLE;
}

NMSettingVerifyResult
_nm_setting_verify (NMSetting *setting, GSList *all_settings, GError **error)
{
	g_return_val_if_fail (NM_IS_SETTING (setting), NM_SETTING_VERIFY_ERROR);
	g_return_val_if_fail (!error || *error == NULL, NM_SETTING_VERIFY_ERROR);

	if (NM_SETTING_GET_CLASS (setting)->verify)
		return NM_SETTING_GET_CLASS (setting)->verify (setting, all_settings, error);

	return NM_SETTING_VERIFY_SUCCESS;
}

static gboolean
compare_property (NMSetting *setting,
                  NMSetting *other,
                  const GParamSpec *prop_spec,
                  NMSettingCompareFlags flags)
{
	const NMSettingProperty *property;
	GValue value1 = G_VALUE_INIT;
	GValue value2 = G_VALUE_INIT;
	int cmp;

	/* Handle compare flags */
	if (prop_spec->flags & NM_SETTING_PARAM_SECRET) {
		NMSettingSecretFlags a_secret_flags = NM_SETTING_SECRET_FLAG_NONE;
		NMSettingSecretFlags b_secret_flags = NM_SETTING_SECRET_FLAG_NONE;

		nm_setting_get_secret_flags (setting, prop_spec->name, &a_secret_flags, NULL);
		nm_setting_get_secret_flags (other, prop_spec->name, &b_secret_flags, NULL);

		/* If the secret flags aren't the same the settings aren't the same */
		if (a_secret_flags != b_secret_flags)
			return FALSE;

		/* Check for various secret flags that might cause us to ignore comparing
		 * this property.
		 */
		if (   (flags & NM_SETTING_COMPARE_FLAG_IGNORE_AGENT_OWNED_SECRETS)
		    && (a_secret_flags & NM_SETTING_SECRET_FLAG_AGENT_OWNED))
			return TRUE;

		if (   (flags & NM_SETTING_COMPARE_FLAG_IGNORE_NOT_SAVED_SECRETS)
		    && (a_secret_flags & NM_SETTING_SECRET_FLAG_NOT_SAVED))
			return TRUE;
	}

	property = nm_setting_class_find_property (NM_SETTING_GET_CLASS (setting), prop_spec->name);
	g_return_val_if_fail (property != NULL, FALSE);

	g_value_init (&value1, prop_spec->value_type);
	g_object_get_property (G_OBJECT (setting), prop_spec->name, &value1);

	g_value_init (&value2, prop_spec->value_type);
	g_object_get_property (G_OBJECT (other), prop_spec->name, &value2);

	if (property->to_dbus) {
		GValue dbus_value1 = G_VALUE_INIT, dbus_value2 = G_VALUE_INIT;

		g_value_init (&dbus_value1, property->dbus_type);
		property->to_dbus (&value1, &dbus_value1);
		g_value_init (&dbus_value2, property->dbus_type);
		property->to_dbus (&value2, &dbus_value2);

		cmp = nm_property_compare (&dbus_value1, &dbus_value2);

		g_value_unset (&dbus_value1);
		g_value_unset (&dbus_value2);
	} else
		cmp = nm_property_compare (&value1, &value2);

	g_value_unset (&value1);
	g_value_unset (&value2);

	return cmp == 0;
}

/**
 * nm_setting_compare:
 * @a: a #NMSetting
 * @b: a second #NMSetting to compare with the first
 * @flags: compare flags, e.g. %NM_SETTING_COMPARE_FLAG_EXACT
 *
 * Compares two #NMSetting objects for similarity, with comparison behavior
 * modified by a set of flags.  See the documentation for #NMSettingCompareFlags
 * for a description of each flag's behavior.
 *
 * Returns: %TRUE if the comparison succeeds, %FALSE if it does not
 **/
gboolean
nm_setting_compare (NMSetting *a,
                    NMSetting *b,
                    NMSettingCompareFlags flags)
{
	GParamSpec **property_specs;
	guint n_property_specs;
	gint same = TRUE;
	guint i;

	g_return_val_if_fail (NM_IS_SETTING (a), FALSE);
	g_return_val_if_fail (NM_IS_SETTING (b), FALSE);

	/* First check that both have the same type */
	if (G_OBJECT_TYPE (a) != G_OBJECT_TYPE (b))
		return FALSE;

	/* And now all properties */
	property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (a), &n_property_specs);
	for (i = 0; i < n_property_specs && same; i++) {
		GParamSpec *prop_spec = property_specs[i];

		/* Fuzzy compare ignores secrets and properties defined with the FUZZY_IGNORE flag */
		if (   (flags & NM_SETTING_COMPARE_FLAG_FUZZY)
		    && (prop_spec->flags & (NM_SETTING_PARAM_FUZZY_IGNORE | NM_SETTING_PARAM_SECRET)))
			continue;

		if ((flags & NM_SETTING_COMPARE_FLAG_INFERRABLE) && !(prop_spec->flags & NM_SETTING_PARAM_INFERRABLE))
			continue;

		if (   (flags & NM_SETTING_COMPARE_FLAG_IGNORE_SECRETS)
		    && (prop_spec->flags & NM_SETTING_PARAM_SECRET))
			continue;

		same = NM_SETTING_GET_CLASS (a)->compare_property (a, b, prop_spec, flags);
	}
	g_free (property_specs);

	return same;
}

static inline gboolean
should_compare_prop (NMSetting *setting,
                     const char *prop_name,
                     NMSettingCompareFlags comp_flags,
                     GParamFlags prop_flags)
{
	/* Fuzzy compare ignores secrets and properties defined with the FUZZY_IGNORE flag */
	if (   (comp_flags & NM_SETTING_COMPARE_FLAG_FUZZY)
	    && (prop_flags & (NM_SETTING_PARAM_FUZZY_IGNORE | NM_SETTING_PARAM_SECRET)))
		return FALSE;

	if ((comp_flags & NM_SETTING_COMPARE_FLAG_INFERRABLE) && !(prop_flags & NM_SETTING_PARAM_INFERRABLE))
		return FALSE;

	if (prop_flags & NM_SETTING_PARAM_SECRET) {
		NMSettingSecretFlags secret_flags = NM_SETTING_SECRET_FLAG_NONE;

		if (comp_flags & NM_SETTING_COMPARE_FLAG_IGNORE_SECRETS)
			return FALSE;

		nm_setting_get_secret_flags (setting, prop_name, &secret_flags, NULL);

		if (   (comp_flags & NM_SETTING_COMPARE_FLAG_IGNORE_AGENT_OWNED_SECRETS)
		    && (secret_flags & NM_SETTING_SECRET_FLAG_AGENT_OWNED))
			return FALSE;

		if (   (comp_flags & NM_SETTING_COMPARE_FLAG_IGNORE_NOT_SAVED_SECRETS)
		    && (secret_flags & NM_SETTING_SECRET_FLAG_NOT_SAVED))
			return FALSE;
	}

	if (   (comp_flags & NM_SETTING_COMPARE_FLAG_IGNORE_ID)
	    && NM_IS_SETTING_CONNECTION (setting)
	    && !strcmp (prop_name, NM_SETTING_CONNECTION_ID))
		return FALSE;

	return TRUE;
}

/**
 * nm_setting_diff:
 * @a: a #NMSetting
 * @b: a second #NMSetting to compare with the first
 * @flags: compare flags, e.g. %NM_SETTING_COMPARE_FLAG_EXACT
 * @invert_results: this parameter is used internally by libnm and should
 * be set to %FALSE.  If %TRUE inverts the meaning of the #NMSettingDiffResult.
 * @results: (inout) (transfer full) (element-type utf8 guint32): if the
 * settings differ, on return a hash table mapping the differing keys to one or
 * more %NMSettingDiffResult values OR-ed together.  If the settings do not
 * differ, any hash table passed in is unmodified.  If no hash table is passed
 * in and the settings differ, a new one is created and returned.
 *
 * Compares two #NMSetting objects for similarity, with comparison behavior
 * modified by a set of flags.  See the documentation for #NMSettingCompareFlags
 * for a description of each flag's behavior.  If the settings differ, the keys
 * of each setting that differ from the other are added to @results, mapped to
 * one or more #NMSettingDiffResult values.
 *
 * Returns: %TRUE if the settings contain the same values, %FALSE if they do not
 **/
gboolean
nm_setting_diff (NMSetting *a,
                 NMSetting *b,
                 NMSettingCompareFlags flags,
                 gboolean invert_results,
                 GHashTable **results)
{
	GParamSpec **property_specs;
	guint n_property_specs;
	guint i;
	NMSettingDiffResult a_result = NM_SETTING_DIFF_RESULT_IN_A;
	NMSettingDiffResult b_result = NM_SETTING_DIFF_RESULT_IN_B;
	gboolean results_created = FALSE;

	g_return_val_if_fail (results != NULL, FALSE);
	g_return_val_if_fail (NM_IS_SETTING (a), FALSE);
	if (b) {
		g_return_val_if_fail (NM_IS_SETTING (b), FALSE);
		g_return_val_if_fail (G_OBJECT_TYPE (a) == G_OBJECT_TYPE (b), FALSE);
	}

	/* If the caller is calling this function in a pattern like this to get
	 * complete diffs:
	 *
	 * nm_setting_diff (A, B, FALSE, &results);
	 * nm_setting_diff (B, A, TRUE, &results);
	 *
	 * and wants us to invert the results so that the second invocation comes
	 * out correctly, do that here.
	 */
	if (invert_results) {
		a_result = NM_SETTING_DIFF_RESULT_IN_B;
		b_result = NM_SETTING_DIFF_RESULT_IN_A;
	}

	if (*results == NULL) {
		*results = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		results_created = TRUE;
	}

	/* And now all properties */
	property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (a), &n_property_specs);

	for (i = 0; i < n_property_specs; i++) {
		GParamSpec *prop_spec = property_specs[i];
		NMSettingDiffResult r = NM_SETTING_DIFF_RESULT_UNKNOWN, tmp;
		gboolean different = TRUE;

		/* Handle compare flags */
		if (!should_compare_prop (a, prop_spec->name, flags, prop_spec->flags))
			continue;
		if (strcmp (prop_spec->name, NM_SETTING_NAME) == 0)
			continue;

		if (b) {
			different = !NM_SETTING_GET_CLASS (a)->compare_property (a, b, prop_spec, flags);
			if (different) {
				GValue value = G_VALUE_INIT;

				g_value_init (&value, prop_spec->value_type);
				g_object_get_property (G_OBJECT (a), prop_spec->name, &value);
				if (!g_param_value_defaults (prop_spec, &value))
					r |= a_result;

				g_value_reset (&value);
				g_object_get_property (G_OBJECT (b), prop_spec->name, &value);
				if (!g_param_value_defaults (prop_spec, &value))
					r |= b_result;

				g_value_unset (&value);
			}
		} else
			r = a_result;  /* only in A */

		if (different) {
			tmp = GPOINTER_TO_UINT (g_hash_table_lookup (*results, prop_spec->name));
			g_hash_table_insert (*results, g_strdup (prop_spec->name), GUINT_TO_POINTER (tmp | r));
		}
	}
	g_free (property_specs);

	/* Don't return an empty hash table */
	if (results_created && !g_hash_table_size (*results)) {
		g_hash_table_destroy (*results);
		*results = NULL;
	}

	return !(*results);
}

/**
 * nm_setting_enumerate_values:
 * @setting: the #NMSetting
 * @func: (scope call): user-supplied function called for each property of the setting
 * @user_data: user data passed to @func at each invocation
 *
 * Iterates over each property of the #NMSetting object, calling the supplied
 * user function for each property.
 **/
void
nm_setting_enumerate_values (NMSetting *setting,
                             NMSettingValueIterFn func,
                             gpointer user_data)
{
	GParamSpec **property_specs;
	guint n_property_specs;
	int i;

	g_return_if_fail (NM_IS_SETTING (setting));
	g_return_if_fail (func != NULL);

	property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (setting), &n_property_specs);
	for (i = 0; i < n_property_specs; i++) {
		GParamSpec *prop_spec = property_specs[i];
		GValue value = G_VALUE_INIT;

		g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (prop_spec));
		g_object_get_property (G_OBJECT (setting), prop_spec->name, &value);
		func (setting, prop_spec->name, &value, prop_spec->flags, user_data);
		g_value_unset (&value);
	}

	g_free (property_specs);
}

/**
 * nm_setting_clear_secrets:
 * @setting: the #NMSetting
 *
 * Resets and clears any secrets in the setting.  Secrets should be added to the
 * setting only when needed, and cleared immediately after use to prevent
 * leakage of information.
 **/
void
nm_setting_clear_secrets (NMSetting *setting)
{
	_nm_setting_clear_secrets (setting);
}

gboolean
_nm_setting_clear_secrets (NMSetting *setting)
{
	GParamSpec **property_specs;
	guint n_property_specs;
	guint i;
	gboolean changed = FALSE;

	g_return_val_if_fail (NM_IS_SETTING (setting), FALSE);

	property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (setting), &n_property_specs);

	for (i = 0; i < n_property_specs; i++) {
		GParamSpec *prop_spec = property_specs[i];

		if (prop_spec->flags & NM_SETTING_PARAM_SECRET) {
			GValue value = G_VALUE_INIT;

			g_value_init (&value, prop_spec->value_type);
			g_object_get_property (G_OBJECT (setting), prop_spec->name, &value);
			if (!g_param_value_defaults (prop_spec, &value)) {
				g_param_value_set_default (prop_spec, &value);
				g_object_set_property (G_OBJECT (setting), prop_spec->name, &value);
				changed = TRUE;
			}
			g_value_unset (&value);
		}
	}

	g_free (property_specs);

	return changed;
}

static gboolean
clear_secrets_with_flags (NMSetting *setting,
                          GParamSpec *pspec,
                          NMSettingClearSecretsWithFlagsFn func,
                          gpointer user_data)
{
	NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_NONE;
	gboolean changed = FALSE;

	/* Clear the secret if the user function says to do so */
	nm_setting_get_secret_flags (setting, pspec->name, &flags, NULL);
	if (func (setting, pspec->name, flags, user_data) == TRUE) {
		GValue value = G_VALUE_INIT;

		g_value_init (&value, pspec->value_type);
		g_object_get_property (G_OBJECT (setting), pspec->name, &value);
		if (!g_param_value_defaults (pspec, &value)) {
			g_param_value_set_default (pspec, &value);
			g_object_set_property (G_OBJECT (setting), pspec->name, &value);
			changed = TRUE;
		}
		g_value_unset (&value);
	}

	return changed;
}

/**
 * nm_setting_clear_secrets_with_flags:
 * @setting: the #NMSetting
 * @func: (scope call): function to be called to determine whether a
 *     specific secret should be cleared or not
 * @user_data: caller-supplied data passed to @func
 *
 * Clears and frees secrets determined by @func.
 **/
void
nm_setting_clear_secrets_with_flags (NMSetting *setting,
                                     NMSettingClearSecretsWithFlagsFn func,
                                     gpointer user_data)
{
	_nm_setting_clear_secrets_with_flags (setting, func, user_data);
}

gboolean
_nm_setting_clear_secrets_with_flags (NMSetting *setting,
                                      NMSettingClearSecretsWithFlagsFn func,
                                      gpointer user_data)
{
	GParamSpec **property_specs;
	guint n_property_specs;
	guint i;
	gboolean changed = FALSE;

	g_return_val_if_fail (setting, FALSE);
	g_return_val_if_fail (NM_IS_SETTING (setting), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (setting), &n_property_specs);
	for (i = 0; i < n_property_specs; i++) {
		if (property_specs[i]->flags & NM_SETTING_PARAM_SECRET) {
			changed |= NM_SETTING_GET_CLASS (setting)->clear_secrets_with_flags (setting,
			                                                                     property_specs[i],
			                                                                     func,
			                                                                     user_data);
		}
	}

	g_free (property_specs);
	return changed;
}

/**
 * nm_setting_need_secrets:
 * @setting: the #NMSetting
 *
 * Returns an array of property names for each secret which may be required
 * to make a successful connection.  The returned hints are only intended as a
 * guide to what secrets may be required, because in some circumstances, there
 * is no way to conclusively determine exactly which secrets are needed.
 *
 * Returns: (transfer container) (element-type utf8): a #GPtrArray containing
 * the property names of secrets of the #NMSetting which may be required; the
 * caller owns the array and must free it with g_ptr_array_free(), but must not
 * free the elements.
 **/
GPtrArray *
nm_setting_need_secrets (NMSetting *setting)
{
	GPtrArray *secrets = NULL;

	g_return_val_if_fail (NM_IS_SETTING (setting), NULL);

	if (NM_SETTING_GET_CLASS (setting)->need_secrets)
		secrets = NM_SETTING_GET_CLASS (setting)->need_secrets (setting);

	return secrets;
}

static int
update_one_secret (NMSetting *setting, const char *key, GValue *value, GError **error)
{
	const NMSettingProperty *property;
	GParamSpec *prop_spec;

	property = nm_setting_class_find_property (NM_SETTING_GET_CLASS (setting), key);
	if (!property) {
		g_set_error (error,
		             NM_SETTING_ERROR,
		             NM_SETTING_ERROR_PROPERTY_NOT_FOUND,
		             "%s", key);
		return NM_SETTING_UPDATE_SECRET_ERROR;
	}

	/* Silently ignore non-secrets */
	prop_spec = property->param_spec;
	if (!prop_spec || !(prop_spec->flags & NM_SETTING_PARAM_SECRET))
		return NM_SETTING_UPDATE_SECRET_SUCCESS_UNCHANGED;

	if (g_value_type_compatible (G_VALUE_TYPE (value), G_PARAM_SPEC_VALUE_TYPE (prop_spec))) {
		if (G_VALUE_HOLDS_STRING (value) && G_IS_PARAM_SPEC_STRING (prop_spec)) {
			/* String is expected to be a common case. Handle it specially and check whether
			 * the value is already set. Otherwise, we just reset the property and
			 * assume the value got modified. */
			char *v;

			g_object_get (G_OBJECT (setting), prop_spec->name, &v, NULL);
			if (g_strcmp0 (v, g_value_get_string (value)) == 0) {
				g_free (v);
				return NM_SETTING_UPDATE_SECRET_SUCCESS_UNCHANGED;
			}
			g_free (v);
		}
		g_object_set_property (G_OBJECT (setting), prop_spec->name, value);
		return NM_SETTING_UPDATE_SECRET_SUCCESS_MODIFIED;
	}
	g_set_error (error,
	             NM_SETTING_ERROR,
	             NM_SETTING_ERROR_PROPERTY_TYPE_MISMATCH,
	             "%s", key);
	return NM_SETTING_UPDATE_SECRET_ERROR;
}

/**
 * _nm_setting_update_secrets:
 * @setting: the #NMSetting
 * @secrets: (element-type utf8 GObject.Value): a #GHashTable mapping
 * string to #GValue of setting property names and secrets
 * @error: location to store error, or %NULL
 *
 * Update the setting's secrets, given a hash table of secrets intended for that
 * setting (deserialized from D-Bus for example).
 *
 * Returns: an #NMSettingUpdateSecretResult
 * update one or more of the secrets.
 **/
NMSettingUpdateSecretResult
_nm_setting_update_secrets (NMSetting *setting, GHashTable *secrets, GError **error)
{
	GHashTableIter iter;
	gpointer key, data;
	GError *tmp_error = NULL;
	NMSettingUpdateSecretResult result = NM_SETTING_UPDATE_SECRET_SUCCESS_UNCHANGED;

	g_return_val_if_fail (NM_IS_SETTING (setting), NM_SETTING_UPDATE_SECRET_ERROR);
	g_return_val_if_fail (secrets != NULL, NM_SETTING_UPDATE_SECRET_ERROR);
	if (error)
		g_return_val_if_fail (*error == NULL, NM_SETTING_UPDATE_SECRET_ERROR);

	g_hash_table_iter_init (&iter, secrets);
	while (g_hash_table_iter_next (&iter, &key, &data)) {
		int success;
		const char *secret_key = (const char *) key;
		GValue *secret_value = (GValue *) data;

		success = NM_SETTING_GET_CLASS (setting)->update_one_secret (setting, secret_key, secret_value, &tmp_error);
		g_assert (!((success == NM_SETTING_UPDATE_SECRET_ERROR) ^ (!!tmp_error)));

		if (success == NM_SETTING_UPDATE_SECRET_ERROR) {
			g_propagate_error (error, tmp_error);
			return NM_SETTING_UPDATE_SECRET_ERROR;
		}

		if (success == NM_SETTING_UPDATE_SECRET_SUCCESS_MODIFIED)
			result = NM_SETTING_UPDATE_SECRET_SUCCESS_MODIFIED;
	}

	return result;
}

static gboolean
is_secret_prop (NMSetting *setting, const char *secret_name, GError **error)
{
	const NMSettingProperty *property;
	GParamSpec *pspec;

	property = nm_setting_class_find_property (NM_SETTING_GET_CLASS (setting), secret_name);
	if (!property) {
		g_set_error (error,
		             NM_SETTING_ERROR,
		             NM_SETTING_ERROR_PROPERTY_NOT_FOUND,
		             "Secret %s not provided by this setting", secret_name);
		return FALSE;
	}

	pspec = property->param_spec;
	if (!pspec || !(pspec->flags & NM_SETTING_PARAM_SECRET)) {
		g_set_error (error,
		             NM_SETTING_ERROR,
		             NM_SETTING_ERROR_PROPERTY_NOT_SECRET,
		             "Property %s is not a secret", secret_name);
		return FALSE;
	}

	return TRUE;
}

static gboolean
get_secret_flags (NMSetting *setting,
                  const char *secret_name,
                  gboolean verify_secret,
                  NMSettingSecretFlags *out_flags,
                  GError **error)
{
	char *flags_prop;
	NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_NONE;

	if (verify_secret)
		g_return_val_if_fail (is_secret_prop (setting, secret_name, error), FALSE);

	flags_prop = g_strdup_printf ("%s-flags", secret_name);
	g_object_get (G_OBJECT (setting), flags_prop, &flags, NULL);
	g_free (flags_prop);

	if (out_flags)
		*out_flags = flags;
	return TRUE;
}

/**
 * nm_setting_get_secret_flags:
 * @setting: the #NMSetting
 * @secret_name: the secret key name to get flags for
 * @out_flags: on success, the #NMSettingSecretFlags for the secret
 * @error: location to store error, or %NULL
 *
 * For a given secret, retrieves the #NMSettingSecretFlags describing how to
 * handle that secret.
 *
 * Returns: %TRUE on success (if the given secret name was a valid property of
 * this setting, and if that property is secret), %FALSE if not
 **/
gboolean
nm_setting_get_secret_flags (NMSetting *setting,
                             const char *secret_name,
                             NMSettingSecretFlags *out_flags,
                             GError **error)
{
	g_return_val_if_fail (NM_IS_SETTING (setting), FALSE);
	g_return_val_if_fail (secret_name != NULL, FALSE);

	return NM_SETTING_GET_CLASS (setting)->get_secret_flags (setting, secret_name, TRUE, out_flags, error);
}

static gboolean
set_secret_flags (NMSetting *setting,
                  const char *secret_name,
                  gboolean verify_secret,
                  NMSettingSecretFlags flags,
                  GError **error)
{
	char *flags_prop;

	if (verify_secret)
		g_return_val_if_fail (is_secret_prop (setting, secret_name, error), FALSE);

	flags_prop = g_strdup_printf ("%s-flags", secret_name);
	g_object_set (G_OBJECT (setting), flags_prop, flags, NULL);
	g_free (flags_prop);
	return TRUE;
}

/**
 * nm_setting_set_secret_flags:
 * @setting: the #NMSetting
 * @secret_name: the secret key name to set flags for
 * @flags: the #NMSettingSecretFlags for the secret
 * @error: location to store error, or %NULL
 *
 * For a given secret, stores the #NMSettingSecretFlags describing how to
 * handle that secret.
 *
 * Returns: %TRUE on success (if the given secret name was a valid property of
 * this setting, and if that property is secret), %FALSE if not
 **/
gboolean
nm_setting_set_secret_flags (NMSetting *setting,
                             const char *secret_name,
                             NMSettingSecretFlags flags,
                             GError **error)
{
	g_return_val_if_fail (NM_IS_SETTING (setting), FALSE);
	g_return_val_if_fail (secret_name != NULL, FALSE);
	g_return_val_if_fail (flags <= NM_SETTING_SECRET_FLAGS_ALL, FALSE);

	return NM_SETTING_GET_CLASS (setting)->set_secret_flags (setting, secret_name, TRUE, flags, error);
}

/**
 * nm_setting_to_string:
 * @setting: the #NMSetting
 *
 * Convert the setting into a string.  For debugging purposes ONLY, should NOT
 * be used for serialization of the setting, or machine-parsed in any way. The
 * output format is not guaranteed to be stable and may change at any time.
 *
 * Returns: an allocated string containing a textual representation of the
 * setting's properties and values (including secrets!), which the caller should
 * free with g_free()
 **/
char *
nm_setting_to_string (NMSetting *setting)
{
	GString *string;
	GParamSpec **property_specs;
	guint n_property_specs;
	guint i;

	g_return_val_if_fail (NM_IS_SETTING (setting), NULL);

	property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (setting), &n_property_specs);

	string = g_string_new (nm_setting_get_name (setting));
	g_string_append_c (string, '\n');

	for (i = 0; i < n_property_specs; i++) {
		GParamSpec *prop_spec = property_specs[i];
		GValue value = G_VALUE_INIT;
		char *value_str;
		gboolean is_default;

		if (strcmp (prop_spec->name, NM_SETTING_NAME) == 0)
			continue;

		g_value_init (&value, prop_spec->value_type);
		g_object_get_property (G_OBJECT (setting), prop_spec->name, &value);

		value_str = g_strdup_value_contents (&value);
		g_string_append_printf (string, "\t%s : %s", prop_spec->name, value_str);
		g_free (value_str);

		is_default = g_param_value_defaults (prop_spec, &value);
		g_value_unset (&value);

		g_string_append (string, " (");
		g_string_append_c (string, 's');
		if (is_default)
			g_string_append_c (string, 'd');
		g_string_append_c (string, ')');
		g_string_append_c (string, '\n');
	}

	g_free (property_specs);
	g_string_append_c (string, '\n');

	return g_string_free (string, FALSE);
}

NMSetting *
_nm_setting_find_in_list_required (GSList *all_settings,
                                   const char *setting_name,
                                   GError **error,
                                   const char *error_prefix_setting_name,
                                   const char *error_prefix_property_name)
{
	NMSetting *setting;

	g_return_val_if_fail (!error || !*error, NULL);
	g_return_val_if_fail (all_settings, NULL);
	g_return_val_if_fail (setting_name, NULL);
	g_return_val_if_fail (!error_prefix_setting_name == !error_prefix_property_name, NULL);

	setting = nm_setting_find_in_list (all_settings, setting_name);
	if (!setting) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             !strcmp (setting_name, NM_SETTING_CONNECTION_SETTING_NAME)
		                 ? NM_CONNECTION_ERROR_CONNECTION_SETTING_NOT_FOUND
		                 : NM_CONNECTION_ERROR_SETTING_NOT_FOUND,
		             _("Missing '%s' setting"),
		             setting_name);
		if (error_prefix_setting_name)
			g_prefix_error (error, "%s.%s: ", error_prefix_setting_name, error_prefix_property_name);
	}
	return setting;
}

NMSettingVerifyResult
_nm_setting_verify_required_virtual_interface_name (GSList *all_settings,
                                                    GError **error)
{
	NMSettingConnection *s_con;
	const char *interface_name;

	s_con = NM_SETTING_CONNECTION (nm_setting_find_in_list (all_settings, NM_SETTING_CONNECTION_SETTING_NAME));
	interface_name = s_con ? nm_setting_connection_get_interface_name (s_con) : NULL;
	if (!interface_name) {
		g_set_error_literal (error,
		                     NM_SETTING_CONNECTION_ERROR,
		                     NM_SETTING_CONNECTION_ERROR_MISSING_PROPERTY,
		                     _("property is missing"));
		g_prefix_error (error, "%s.%s: ", NM_SETTING_CONNECTION_SETTING_NAME, NM_SETTING_CONNECTION_INTERFACE_NAME);
		return NM_SETTING_VERIFY_ERROR;
	}

	return NM_SETTING_VERIFY_SUCCESS;
}

gboolean
_nm_setting_get_deprecated_virtual_interface_name (NMSetting *setting,
                                                   NMConnection *connection,
                                                   const char *property,
                                                   GValue *value)
{
	NMSettingConnection *s_con;

	s_con = nm_connection_get_setting_connection (connection);
	g_return_val_if_fail (s_con != NULL, FALSE);

	if (nm_setting_connection_get_interface_name (s_con)) {
		g_value_set_string (value, nm_setting_connection_get_interface_name (s_con));
		return TRUE;
	} else
		return FALSE;
}

gboolean
_nm_setting_set_deprecated_virtual_interface_name (NMSetting *setting,
                                                   GHashTable *connection_hash,
                                                   const char *property,
                                                   const GValue *value,
                                                   GError **error)
{
	const char *interface_name;
	GQuark error_domain;
	char *error_enum_name;
	GEnumClass *enum_class;
	GEnumValue *enum_val;
	int error_code = 0;

	/* If the virtual setting type hash contains an interface name, it must be
	 * valid (even if it's going to be ignored in favor of
	 * NMSettingConnection:interface-name). Other than that, we don't have to
	 * check anything here; NMSettingConnection:interface-name will do the rest.
	 */
	interface_name = g_value_get_string (value);
	if (!interface_name || nm_utils_iface_valid_name (interface_name))
		return TRUE;

	/* For compatibility reasons, we have to use the right error domain... */
	error_domain = _nm_setting_lookup_error_quark (nm_setting_get_name (setting));
	error_enum_name = g_strdup_printf ("%sError", G_OBJECT_TYPE_NAME (setting));
	enum_class = g_type_class_ref (g_type_from_name (error_enum_name));
	g_free (error_enum_name);
	if (enum_class) {
		enum_val = g_enum_get_value_by_nick (enum_class, "InvalidProperty");
		if (enum_val)
			error_code = enum_val->value;
		g_type_class_unref (enum_class);
	}

	g_set_error_literal (error, error_domain, error_code,
	                     _("invalid value in compatibility property"));
	g_prefix_error (error, "%s.%s: ", nm_setting_get_name (setting), property);
	return FALSE;
}

/*****************************************************************************/

static void
nm_setting_init (NMSetting *setting)
{
}

static void
constructed (GObject *object)
{
	_ensure_setting_info (object, NM_SETTING_GET_PRIVATE (object));

	G_OBJECT_CLASS (nm_setting_parent_class)->constructed (object);
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMSetting *setting = NM_SETTING (object);

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, nm_setting_get_name (setting));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_setting_class_init (NMSettingClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);

	if (!setting_property_overrides_quark)
		setting_property_overrides_quark = g_quark_from_static_string ("nm-setting-property-overrides");
	if (!setting_properties_quark)
		setting_properties_quark = g_quark_from_static_string ("nm-setting-properties");

	g_type_class_add_private (setting_class, sizeof (NMSettingPrivate));

	/* virtual methods */
	object_class->constructed  = constructed;
	object_class->get_property = get_property;

	setting_class->update_one_secret = update_one_secret;
	setting_class->get_secret_flags = get_secret_flags;
	setting_class->set_secret_flags = set_secret_flags;
	setting_class->compare_property = compare_property;
	setting_class->clear_secrets_with_flags = clear_secrets_with_flags;

	/* Properties */

	/**
	 * NMSetting:name:
	 *
	 * The setting's name, which uniquely identifies the setting within the
	 * connection.  Each setting type has a name unique to that type, for
	 * example "ppp" or "wireless" or "wired".
	 **/
	g_object_class_install_property
		(object_class, PROP_NAME,
		 g_param_spec_string (NM_SETTING_NAME, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));
}
