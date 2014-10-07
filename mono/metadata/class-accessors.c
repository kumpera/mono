/* 
 * Copyright 2014 Xamarin Inc
 */
#include <mono/metadata/class-internals.h>
#include <mono/metadata/tabledefs.h>

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


/**
 * mono_class_get_flags:
 * @klass: the MonoClass to act on
 *
 * The type flags from the TypeDef table from the metadata.
 * see the TYPE_ATTRIBUTE_* definitions on tabledefs.h for the
 * different values.
 *
 * Returns: the flags from the TypeDef table.
 */
guint32
mono_class_get_flags (MonoClass *klass)
{
	switch (klass->class_kind) {
	case MONO_CLASS_BORING:
	case MONO_CLASS_GTD:
		return ((MonoClassBoring*)klass)->flags;
	case MONO_CLASS_GINST:
		return mono_class_get_flags (((MonoClassGenericInst*)klass)->generic_class->container_class);
	case MONO_CLASS_GPARAM:
		return TYPE_ATTRIBUTE_PUBLIC;
	case MONO_CLASS_ARRAY:
		/* all arrays are marked serializable and sealed, bug #42779 */
		return TYPE_ATTRIBUTE_CLASS | TYPE_ATTRIBUTE_SERIALIZABLE | TYPE_ATTRIBUTE_SEALED | TYPE_ATTRIBUTE_PUBLIC;
	case MONO_CLASS_POINTER:
		return TYPE_ATTRIBUTE_CLASS | (mono_class_get_flags (klass->element_class) & TYPE_ATTRIBUTE_VISIBILITY_MASK);
	}
	g_assert_not_reached ();
}

void
mono_class_set_flags (MonoClass *klass, guint32 flags)
{
	g_assert (klass->class_kind == MONO_CLASS_BORING || klass->class_kind == MONO_CLASS_GTD);
	((MonoClassBoring*)klass)->flags = flags;
}

/*
 * mono_class_get_generic_container:
 *
 *   Return the generic container of KLASS which should be a generic type definition.
 */
MonoGenericContainer*
mono_class_get_generic_container (MonoClass *klass)
{
	return klass->generic_container;
}

MonoGenericContainer*
mono_class_try_get_generic_container (MonoClass *klass)
{
	return klass->generic_container;
}

void
mono_class_set_generic_container (MonoClass *klass, MonoGenericContainer *container)
{
	klass->generic_container = container;
}
