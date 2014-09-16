/* 
 * Copyright 2014 Xamarin Inc
 */
#ifndef __MONO_METADATA_PROPERTY_BAG_H__
#define __MONO_METADATA_PROPERTY_BAG_H__

#include <mono/utils/mono-compiler.h>

typedef struct _MonoPropertyBagItem MonoPropertyBagItem;

struct _MonoPropertyBagItem {
	MonoPropertyBagItem *next;
	int tag;
};

typedef struct {
	MonoPropertyBagItem *head;
} MonoPropertyBag;

void* mono_property_bag_get (MonoPropertyBag *bag, int tag) MONO_INTERNAL;
void* mono_property_bag_add (MonoPropertyBag *bag, void *value) MONO_INTERNAL;

#endif
