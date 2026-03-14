#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ================================================================ */
/* Raw ObjC structures as they appear in Mach-O binaries            */
/* ================================================================ */

/* -- objc_class (on-disk layout, pointer-width agnostic via uint64_t) -- */
typedef struct {
    uint64_t isa;
    uint64_t superclass;
    uint64_t method_cache;
    uint64_t vtable;
    uint64_t data;   /* pointer to class_ro_t; low bits may contain flags */
} ObjCClassRaw;

/* Flags embedded in the low bits of ObjCClassRaw.data */
#define OBJC_CLASS_DATA_MASK  (~(uint64_t)7)
#define OBJC_CLASS_IS_SWIFT   (1UL << 0)

/* -- class_ro_t -- */
typedef struct {
    uint32_t flags;
    uint32_t instance_start;
    uint32_t instance_size;
    /* 4 bytes implicit padding on 64-bit (between instance_size and ivar_layout) */
    uint64_t ivar_layout;
    uint64_t name;
    uint64_t base_methods;
    uint64_t base_protocols;
    uint64_t ivars;
    uint64_t weak_ivar_layout;
    uint64_t base_properties;
} ObjCClassRoRaw64;

typedef struct {
    uint32_t flags;
    uint32_t instance_start;
    uint32_t instance_size;
    uint32_t ivar_layout;
    uint32_t name;
    uint32_t base_methods;
    uint32_t base_protocols;
    uint32_t ivars;
    uint32_t weak_ivar_layout;
    uint32_t base_properties;
} ObjCClassRoRaw32;

/* class_ro_t flag bits */
#define RO_META               (1 << 0)
#define RO_ROOT               (1 << 1)
#define RO_HAS_CXX_STRUCTORS  (1 << 2)

/* -- method_t -- */
typedef struct {
    uint64_t name;
    uint64_t types;
    uint64_t imp;
} ObjCMethodRaw64;

typedef struct {
    uint32_t name;
    uint32_t types;
    uint32_t imp;
} ObjCMethodRaw32;

/* method_list_t header: uint32_t entsizeAndFlags, uint32_t count */
#define METHOD_LIST_FLAG_RELATIVE  0x80000000u
#define METHOD_LIST_ENTSIZE_MASK   0x0000FFFFu

/* Relative method (modern arm64e) */
typedef struct {
    int32_t name_offset;
    int32_t types_offset;
    int32_t imp_offset;
} ObjCRelativeMethod;

/* -- ivar_t -- */
typedef struct {
    uint64_t offset_ptr;
    uint64_t name;
    uint64_t type;
    uint32_t alignment_raw;
    uint32_t size;
} ObjCIvarRaw64;

typedef struct {
    uint32_t offset_ptr;
    uint32_t name;
    uint32_t type;
    uint32_t alignment_raw;
    uint32_t size;
} ObjCIvarRaw32;

/* -- property_t -- */
typedef struct {
    uint64_t name;
    uint64_t attributes;
} ObjCPropertyRaw64;

typedef struct {
    uint32_t name;
    uint32_t attributes;
} ObjCPropertyRaw32;

/* -- protocol_t -- */
typedef struct {
    uint64_t isa;
    uint64_t name;
    uint64_t protocols;
    uint64_t instance_methods;
    uint64_t class_methods;
    uint64_t optional_instance_methods;
    uint64_t optional_class_methods;
    uint64_t instance_properties;
    uint32_t size;
    uint32_t flags;
} ObjCProtocolRaw64;

typedef struct {
    uint32_t isa;
    uint32_t name;
    uint32_t protocols;
    uint32_t instance_methods;
    uint32_t class_methods;
    uint32_t optional_instance_methods;
    uint32_t optional_class_methods;
    uint32_t instance_properties;
    uint32_t size;
    uint32_t flags;
} ObjCProtocolRaw32;

/* -- category_t -- */
typedef struct {
    uint64_t name;
    uint64_t cls;
    uint64_t instance_methods;
    uint64_t class_methods;
    uint64_t protocols;
    uint64_t instance_properties;
} ObjCCategoryRaw64;

typedef struct {
    uint32_t name;
    uint32_t cls;
    uint32_t instance_methods;
    uint32_t class_methods;
    uint32_t protocols;
    uint32_t instance_properties;
} ObjCCategoryRaw32;

/* ================================================================ */
/* Parsed (clean) output structures                                 */
/* ================================================================ */

typedef struct {
    char *name;
    char *types;
    char *return_type;
    bool is_class_method;
} ObjCMethod;

typedef struct {
    char *name;
    char *type;
    uint32_t offset;
    uint32_t size;
} ObjCIvar;

typedef struct {
    char *name;
    char *attributes;
    char *type_name;
    char *getter;
    char *setter;
    bool is_readonly;
    bool is_nonatomic;
    bool is_weak;
    bool is_copy;
    bool is_retain;
    bool is_dynamic;
} ObjCProperty;

typedef struct ObjCProtocolInfo {
    char *name;
    ObjCMethod *instance_methods;
    size_t instance_method_count;
    ObjCMethod *class_methods;
    size_t class_method_count;
    ObjCMethod *optional_instance_methods;
    size_t optional_instance_method_count;
    ObjCMethod *optional_class_methods;
    size_t optional_class_method_count;
    ObjCProperty *properties;
    size_t property_count;
    char **adopted_protocols;
    size_t adopted_protocol_count;
} ObjCProtocolInfo;

typedef struct {
    char *name;
    char *superclass_name;
    bool is_meta;
    bool is_root;
    bool is_swift_class;
    uint32_t instance_size;
    ObjCMethod *instance_methods;
    size_t instance_method_count;
    ObjCMethod *class_methods;
    size_t class_method_count;
    ObjCIvar *ivars;
    size_t ivar_count;
    ObjCProperty *properties;
    size_t property_count;
    char **protocols;
    size_t protocol_count;
} ObjCClassInfo;

typedef struct {
    char *name;
    char *class_name;
    ObjCMethod *instance_methods;
    size_t instance_method_count;
    ObjCMethod *class_methods;
    size_t class_method_count;
    ObjCProperty *properties;
    size_t property_count;
    char **protocols;
    size_t protocol_count;
} ObjCCategoryInfo;

/* Top-level result */
typedef struct {
    ObjCClassInfo *classes;
    size_t class_count;
    ObjCProtocolInfo *protocols;
    size_t protocol_count;
    ObjCCategoryInfo *categories;
    size_t category_count;
    char **selectors;
    size_t selector_count;

    /* Image info from __objc_imageinfo */
    uint32_t objc_version;       /* ObjC ABI version (usually 0) */
    uint32_t objc_flags;         /* Flags including Swift version */
    uint32_t swift_version;      /* Extracted Swift ABI version (0 if none) */
    bool has_image_info;
} ObjCMetadata;

/* ================================================================ */
/* Cleanup functions                                                */
/* ================================================================ */

void objc_method_destroy(ObjCMethod *m);
void objc_ivar_destroy(ObjCIvar *iv);
void objc_property_destroy(ObjCProperty *p);
void objc_protocol_info_destroy(ObjCProtocolInfo *pi);
void objc_class_info_destroy(ObjCClassInfo *ci);
void objc_category_info_destroy(ObjCCategoryInfo *cat);
void objc_metadata_destroy(ObjCMetadata *meta);
