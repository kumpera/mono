/* 
 * Copyright 2014 Xamarin Inc
 */
#include <mono/metadata/class-internals.h>

enum {
	PROP_MARSHAL_INFO = 1,
	PROP_EXT = 2,
	PROP_REF_INFO_HANDLE = 3,
};

MonoMarshalType*
mono_class_get_marshal_info (MonoClass *class)
{
	return mono_property_bag_get (&class->infrequent_data, PROP_MARSHAL_INFO);
}

MonoMarshalType *
mono_class_set_marshal_info (MonoClass *class, MonoMarshalType *info)
{
	info->head.tag = PROP_MARSHAL_INFO;
	return mono_property_bag_add (&class->infrequent_data, info);
}

MonoClassExt *
mono_class_get_ext (MonoClass *class)
{
	return mono_property_bag_get (&class->infrequent_data, PROP_EXT);
}

MonoClassExt *
mono_class_set_ext (MonoClass *class, MonoClassExt *ext)
{
	ext->head.tag = PROP_EXT;
	return mono_property_bag_add (&class->infrequent_data, ext);
}

typedef struct {
	MonoPropertyBagItem head;
	guint32 value;
} Uint32Property;

guint32
mono_class_get_ref_info_handle (MonoClass *class)
{
	Uint32Property *prop = mono_property_bag_get (&class->infrequent_data, PROP_REF_INFO_HANDLE);
	return prop ? prop->value : 0;
}

guint32
mono_class_set_ref_info_handle (MonoClass *class, guint32 value)
{
	Uint32Property *prop = mono_class_alloc (class, sizeof (Uint32Property));
	prop->head.tag = PROP_REF_INFO_HANDLE;
	prop->value = value;
	prop = mono_property_bag_add (&class->infrequent_data, prop);
	return prop->value;
}

/* Accessors based on class kind*/
/*
 * mono_class_get_generic_class:
 *
 *   Return the MonoGenericClass of KLASS, which should be a generic instance.
 */
MonoGenericClass*
mono_class_get_generic_class (MonoClass *klass)
{
	g_assert (mono_class_is_ginst (klass));

	return ((MonoClassGenericInst*)klass)->generic_class;
}

MonoGenericClass*
mono_class_try_get_generic_class (MonoClass *klass)
{
	if (mono_class_is_ginst (klass))
		return ((MonoClassGenericInst*)klass)->generic_class;
	return NULL;
}
