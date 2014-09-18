/* 
 * Copyright 2014 Xamarin Inc
 */
#include <mono/metadata/class-internals.h>

enum {
	PROP_MARSHAL_INFO = 1,
	PROP_EXT = 2,
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
