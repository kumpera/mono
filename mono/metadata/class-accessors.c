/* 
 * Copyright 2014 Xamarin Inc
 */
#include <mono/metadata/class-internals.h>

MonoMarshalType*
mono_class_get_marshal_info (MonoClass *class)
{
	return class->marshal_info;
}

MonoMarshalType *
mono_class_set_marshal_info (MonoClass *class, MonoMarshalType *info)
{
	if (class->marshal_info)
		return class->marshal_info;
	class->marshal_info = info;
	return info;
}

MonoClassExt *
mono_class_get_ext (MonoClass *class)
{
	return class->ext;
}

MonoClassExt *
mono_class_set_ext (MonoClass *class, MonoClassExt *ext)
{
	if (class->ext)
		return class->ext;
	class->ext = ext;
	return ext;
}
