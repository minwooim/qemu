/*
 * QEMU NVM Express Subsystem: nvme-subsys
 *
 * Copyright (c) 2021 Minwoo Im <minwoo.im.dev@gmail.com>
 *
 * This code is licensed under the GNU GPL v2.  Refer COPYING.
 */

#ifndef NVME_SUBSYS_H
#define NVME_SUBSYS_H

#define TYPE_NVME_SUBSYS "nvme-subsys"
#define NVME_SUBSYS(obj) \
    OBJECT_CHECK(NvmeSubsystem, (obj), TYPE_NVME_SUBSYS)

#define NVME_SUBSYS_MAX_CTRLS   32
#define NVME_SUBSYS_MAX_NAMESPACES  32
#define NVME_SUBSYS_MAX_ANA_GROUP   NVME_SUBSYS_MAX_NAMESPACES

typedef struct NvmeCtrl NvmeCtrl;
typedef struct NvmeNamespace NvmeNamespace;

typedef struct NvmeAna {
    uint32_t        grpid;
    uint8_t         state;
#define NVME_SUBSYS_ANA_NSID_BITMAP_SIZE    (NVME_SUBSYS_MAX_ANA_GROUP + 1)
    DECLARE_BITMAP(nsids, NVME_SUBSYS_ANA_NSID_BITMAP_SIZE);
} NvmeAna;

static inline void nvme_subsys_ana_register_ns(NvmeAna *ana, uint32_t nsid)
{
    set_bit(nsid, ana->nsids);
}

static inline bool nvme_subsys_ana_has_ns(NvmeAna *ana, uint32_t nsid)
{
    return test_bit(nsid, ana->nsids);
}

typedef struct NvmeSubsystemParams {
    bool ana;
} NvmeSubsystemParams;

typedef struct NvmeSubsystem {
    DeviceState parent_obj;
    uint8_t     subnqn[256];

    NvmeCtrl    *ctrls[NVME_SUBSYS_MAX_CTRLS];
    NvmeNamespace *namespaces[NVME_SUBSYS_MAX_NAMESPACES];
    NvmeAna     ana[NVME_SUBSYS_MAX_ANA_GROUP + 1];
    uint64_t    ana_change_count;

    NvmeSubsystemParams params;
} NvmeSubsystem;

static inline long nvme_subsys_ana_nr_ns(NvmeAna *ana)
{
    return bitmap_count_one(ana->nsids, NVME_SUBSYS_ANA_NSID_BITMAP_SIZE);
}

int nvme_subsys_register_ctrl(NvmeCtrl *n, Error **errp);
int nvme_subsys_register_ns(NvmeNamespace *ns, Error **errp);

#endif /* NVME_SUBSYS_H */
