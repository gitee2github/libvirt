/*
 * cpu_arm.c: CPU driver for arm CPUs
 *
 * Copyright (C) 2013 Red Hat, Inc.
 * Copyright (C) Canonical Ltd. 2012
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "viralloc.h"
#include "cpu.h"
#include "cpu_map.h"
#include "virstring.h"
#include "virxml.h"

#define VIR_FROM_THIS VIR_FROM_CPU

static const virArch archs[] = {
    VIR_ARCH_ARMV6L,
    VIR_ARCH_ARMV7B,
    VIR_ARCH_ARMV7L,
    VIR_ARCH_AARCH64,
};

typedef struct _virCPUarmFeature virCPUarmFeature;
typedef virCPUarmFeature *virCPUarmFeaturePtr;
struct _virCPUarmFeature {
    char *name;
};

static virCPUarmFeaturePtr
virCPUarmFeatureNew(void)
{
    return g_new0(virCPUarmFeature, 1);
}

static void
virCPUarmFeatureFree(virCPUarmFeaturePtr feature)
{
    if (!feature)
        return;

    g_free(feature->name);
    g_free(feature);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virCPUarmFeature, virCPUarmFeatureFree);

static void
virCPUarmDataClear(virCPUarmData *data)
{
    if (!data)
        return;

    g_free(data->features);
}

static void
virCPUarmDataFree(virCPUDataPtr cpuData)
{
    if (!cpuData)
        return;

    virCPUarmDataClear(&cpuData->data.arm);
    g_free(cpuData);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virCPUData, virCPUarmDataFree);

typedef struct _virCPUarmVendor virCPUarmVendor;
typedef virCPUarmVendor *virCPUarmVendorPtr;
struct _virCPUarmVendor {
    char *name;
    unsigned long value;
};

static virCPUarmVendorPtr
virCPUarmVendorNew(void)
{
    return g_new0(virCPUarmVendor, 1);
}

static void
virCPUarmVendorFree(virCPUarmVendorPtr vendor)
{
    if (!vendor)
        return;

    g_free(vendor->name);
    VIR_FREE(vendor);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virCPUarmVendor, virCPUarmVendorFree);

typedef struct _virCPUarmModel virCPUarmModel;
typedef virCPUarmModel *virCPUarmModelPtr;
struct _virCPUarmModel {
    char *name;
    virCPUarmVendorPtr vendor;
    virCPUarmData data;
};

static virCPUarmModelPtr
virCPUarmModelNew(void)
{
    return g_new0(virCPUarmModel, 1);
}

static void
virCPUarmModelFree(virCPUarmModelPtr model)
{
    if (!model)
        return;

    virCPUarmDataClear(&model->data);
    g_free(model->name);
    g_free(model);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virCPUarmModel, virCPUarmModelFree);

typedef struct _virCPUarmMap virCPUarmMap;
typedef virCPUarmMap *virCPUarmMapPtr;
struct _virCPUarmMap {
    GPtrArray *vendors;
    GPtrArray *models;
    GPtrArray *features;
};

static virCPUarmMapPtr
virCPUarmMapNew(void)
{
    virCPUarmMapPtr map;

    map = g_new0(virCPUarmMap, 1);

    map->vendors = g_ptr_array_new();
    g_ptr_array_set_free_func(map->vendors,
                              (GDestroyNotify) virCPUarmVendorFree);

    map->models = g_ptr_array_new();
    g_ptr_array_set_free_func(map->models,
                              (GDestroyNotify) virCPUarmModelFree);

    map->features = g_ptr_array_new();
    g_ptr_array_set_free_func(map->features,
                              (GDestroyNotify) virCPUarmFeatureFree);

    return map;
}

static void
virCPUarmMapFree(virCPUarmMapPtr map)
{
    if (!map)
        return;

    g_ptr_array_free(map->vendors, TRUE);
    g_ptr_array_free(map->models, TRUE);
    g_ptr_array_free(map->features, TRUE);

    g_free(map);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virCPUarmMap, virCPUarmMapFree);

static virCPUarmVendorPtr
virCPUarmVendorFindByID(virCPUarmMapPtr map,
                        unsigned long vendor_id)
{
    size_t i;

    for (i = 0; i < map->vendors->len; i++) {
        virCPUarmVendorPtr vendor = g_ptr_array_index(map->vendors, i);

        if (vendor->value == vendor_id)
            return vendor;
    }

    return NULL;
}

static virCPUarmVendorPtr
virCPUarmVendorFindByName(virCPUarmMapPtr map,
                          const char *name)
{
    size_t i;

    for (i = 0; i < map->vendors->len; i++) {
        virCPUarmVendorPtr vendor = g_ptr_array_index(map->vendors, i);

        if (STREQ(vendor->name, name))
            return vendor;
    }

    return NULL;
}

static virCPUarmFeaturePtr
virCPUarmMapFeatureFind(virCPUarmMapPtr map,
                        const char *name)
{
    size_t i;

    for (i = 0; i < map->features->len; i++) {
        virCPUarmFeaturePtr feature = g_ptr_array_index(map->features, i);

        if (STREQ(feature->name, name))
            return feature;
    }

    return NULL;
}

static int
virCPUarmMapFeatureParse(xmlXPathContextPtr ctxt G_GNUC_UNUSED,
                         const char *name,
                         void *data)
{
    g_autoptr(virCPUarmFeature) feature = NULL;
    virCPUarmMapPtr map = data;

    if (virCPUarmMapFeatureFind(map, name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("CPU feature %s already defined"), name);
        return -1;
    }

    feature = virCPUarmFeatureNew();
    feature->name = g_strdup(name);

    g_ptr_array_add(map->features, g_steal_pointer(&feature));

    return 0;
}

static int
armCpuDataParseFeatures(virCPUDefPtr cpu,
                        const virCPUarmData *cpuData)
{
    int ret = -1;
    size_t i;
    char **features;

    if (!cpu || !cpuData)
        return ret;

    if (!(features = virStringSplitCount(cpuData->features, " ",
                                         0, &cpu->nfeatures)))
        return ret;

    if (cpu->nfeatures) {
        if (VIR_ALLOC_N(cpu->features, cpu->nfeatures) < 0)
            goto error;

        for (i = 0; i < cpu->nfeatures; i++) {
            cpu->features[i].policy = VIR_CPU_FEATURE_REQUIRE;
            cpu->features[i].name = g_strdup(features[i]);
        }
    }

    ret = 0;

cleanup:
    virStringListFree(features);
    return ret;

error:
    for (i = 0; i < cpu->nfeatures; i++)
        VIR_FREE(cpu->features[i].name);
    VIR_FREE(cpu->features);
    cpu->nfeatures = 0;
    goto cleanup;
}

static int
virCPUarmVendorParse(xmlXPathContextPtr ctxt,
                     const char *name,
                     void *data)
{
    virCPUarmMapPtr map = (virCPUarmMapPtr)data;
    g_autoptr(virCPUarmVendor) vendor = NULL;

    if (virCPUarmVendorFindByName(map, name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("CPU vendor %s already defined"), name);
        return -1;
    }

    vendor = virCPUarmVendorNew();
    vendor->name = g_strdup(name);

    if (virXPathULongHex("string(@value)", ctxt, &vendor->value) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s", _("Missing CPU vendor value"));
        return -1;
    }

    if (virCPUarmVendorFindByID(map, vendor->value)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("CPU vendor value 0x%2lx already defined"), vendor->value);
        return -1;
    }

    g_ptr_array_add(map->vendors, g_steal_pointer(&vendor));

    return 0;
}

static virCPUarmModelPtr
virCPUarmModelFindByPVR(virCPUarmMapPtr map,
                        unsigned long pvr)
{
    size_t i;

    for (i = 0; i < map->models->len; i++) {
        virCPUarmModelPtr model = g_ptr_array_index(map->models, i);

        if (model->data.pvr == pvr)
            return model;
    }

    return NULL;

}

static virCPUarmModelPtr
virCPUarmModelFindByName(virCPUarmMapPtr map,
                         const char *name)
{
    size_t i;

    for (i = 0; i < map->models->len; i++) {
        virCPUarmModelPtr model = g_ptr_array_index(map->models, i);

        if (STREQ(model->name, name))
            return model;
    }

    return NULL;
}

static int
virCPUarmModelParse(xmlXPathContextPtr ctxt,
                    const char *name,
                    void *data)
{
    virCPUarmMapPtr map = (virCPUarmMapPtr)data;
    g_autoptr(virCPUarmModel) model = NULL;
    char *vendor = NULL;

    if (virCPUarmModelFindByName(map, name)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("CPU model %s already defined"), name);
        return -1;
    }

    model = virCPUarmModelNew();
    model->name = g_strdup(name);

    if (virXPathBoolean("boolean(./vendor)", ctxt)) {
        vendor = virXPathString("string(./vendor/@name)", ctxt);
        if (!vendor) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Invalid vendor element in CPU model %s"),
                           name);
            return -1;
        }

        if (!(model->vendor = virCPUarmVendorFindByName(map, vendor))) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Unknown vendor %s referenced by CPU model %s"),
                           vendor, model->name);
            return -1;
        }
    }

    if (!virXPathBoolean("boolean(./pvr)", ctxt)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Missing PVR information for CPU model %s"),
                       model->name);
        return -1;
    }

    if (virXPathULongHex("string(./pvr/@value)", ctxt, &model->data.pvr) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Missing or invalid PVR value in CPU model %s"),
                       model->name);
        return -1;
    }

    g_ptr_array_add(map->models, g_steal_pointer(&model));

    return 0;
}

static virCPUarmMapPtr
virCPUarmLoadMap(void)
{
    g_autoptr(virCPUarmMap) map = NULL;

    map = virCPUarmMapNew();

    if (cpuMapLoad("arm", virCPUarmVendorParse, virCPUarmMapFeatureParse, virCPUarmModelParse, map) < 0)
        return NULL;

    return g_steal_pointer(&map);
}

static virCPUarmMapPtr cpuMap;

int virCPUarmDriverOnceInit(void);
VIR_ONCE_GLOBAL_INIT(virCPUarmDriver);

int
virCPUarmDriverOnceInit(void)
{
    if (!(cpuMap = virCPUarmLoadMap()))
        return -1;

    return 0;
}

static virCPUarmMapPtr
virCPUarmGetMap(void)
{
    if (virCPUarmDriverInitialize() < 0)
        return NULL;

    return cpuMap;
}

static int
virCPUarmDecode(virCPUDefPtr cpu,
                const virCPUarmData *cpuData,
                virDomainCapsCPUModelsPtr models)
{
    virCPUarmMapPtr map;
    virCPUarmModelPtr model;
    virCPUarmVendorPtr vendor = NULL;

    if (!cpuData || !(map = virCPUarmGetMap()))
        return -1;

    if (!(model = virCPUarmModelFindByPVR(map, cpuData->pvr))) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       _("Cannot find CPU model with PVR 0x%03lx"),
                       cpuData->pvr);
        return -1;
    }

    if (!virCPUModelIsAllowed(model->name, models)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("CPU model %s is not supported by hypervisor"),
                       model->name);
        return -1;
    }

    cpu->model = g_strdup(model->name);

    if (cpuData->vendor_id &&
        !(vendor = virCPUarmVendorFindByID(map, cpuData->vendor_id))) {
        virReportError(VIR_ERR_OPERATION_FAILED,
                       _("Cannot find CPU vendor with vendor id 0x%02lx"),
                       cpuData->vendor_id);
        return -1;
    }

    if (vendor)
        cpu->vendor = g_strdup(vendor->name);

    if (cpuData->features &&
        armCpuDataParseFeatures(cpu, cpuData) < 0)
        return -1;

    return 0;
}

static int
virCPUarmDecodeCPUData(virCPUDefPtr cpu,
                       const virCPUData *data,
                       virDomainCapsCPUModelsPtr models)
{
    return virCPUarmDecode(cpu, &data->data.arm, models);
}

static int
virCPUarmUpdate(virCPUDefPtr guest,
                const virCPUDef *host)
{
    int ret = -1;
    virCPUDefPtr updated = NULL;

    if (guest->mode != VIR_CPU_MODE_HOST_MODEL)
        return 0;

    if (!host) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("unknown host CPU model"));
        goto cleanup;
    }

    if (!(updated = virCPUDefCopyWithoutModel(guest)))
        goto cleanup;

    updated->mode = VIR_CPU_MODE_CUSTOM;
    if (virCPUDefCopyModel(updated, host, true) < 0)
        goto cleanup;

    virCPUDefStealModel(guest, updated, false);
    guest->mode = VIR_CPU_MODE_CUSTOM;
    guest->match = VIR_CPU_MATCH_EXACT;
    ret = 0;

cleanup:
    virCPUDefFree(updated);
    return ret;
}


static virCPUDefPtr
virCPUarmBaseline(virCPUDefPtr *cpus,
                  unsigned int ncpus G_GNUC_UNUSED,
                  virDomainCapsCPUModelsPtr models G_GNUC_UNUSED,
                  const char **features G_GNUC_UNUSED,
                  bool migratable G_GNUC_UNUSED)
{
    virCPUDefPtr cpu = NULL;

    cpu = virCPUDefNew();

    cpu->model = g_strdup(cpus[0]->model);

    cpu->type = VIR_CPU_TYPE_GUEST;
    cpu->match = VIR_CPU_MATCH_EXACT;

    return cpu;
}

static virCPUCompareResult
virCPUarmCompare(virCPUDefPtr host G_GNUC_UNUSED,
                 virCPUDefPtr cpu G_GNUC_UNUSED,
                 bool failMessages G_GNUC_UNUSED)
{
    return VIR_CPU_COMPARE_IDENTICAL;
}

static int
virCPUarmValidateFeatures(virCPUDefPtr cpu)
{
    virCPUarmMapPtr map;
    size_t i;

    if (!(map = virCPUarmGetMap()))
        return -1;

    for (i = 0; i < cpu->nfeatures; i++) {
        virCPUFeatureDefPtr feature = &cpu->features[i];

        if (!virCPUarmMapFeatureFind(map, feature->name)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("unknown CPU feature: %s"),
                           feature->name);
            return -1;
        }
    }

    return 0;
}

struct cpuArchDriver cpuDriverArm = {
    .name = "arm",
    .arch = archs,
    .narch = G_N_ELEMENTS(archs),
    .compare = virCPUarmCompare,
    .decode = virCPUarmDecodeCPUData,
    .encode = NULL,
    .dataFree = virCPUarmDataFree,
    .baseline = virCPUarmBaseline,
    .update = virCPUarmUpdate,
    .validateFeatures = virCPUarmValidateFeatures,
};
