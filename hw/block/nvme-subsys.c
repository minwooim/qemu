/*
 * QEMU NVM Express Subsystem: nvme-subsys
 *
 * Copyright (c) 2021 Minwoo Im <minwoo.im.dev@gmail.com>
 *
 * This code is licensed under the GNU GPL v2.  Refer COPYING.
 */

#include "qemu/units.h"
#include "qemu/osdep.h"
#include "qemu/uuid.h"
#include "qemu/iov.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "hw/block/block.h"
#include "block/aio.h"
#include "block/accounting.h"
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "hw/pci/pci.h"
#include "nvme.h"
#include "nvme-subsys.h"

static void nvme_subsys_ana_state_change(NvmeSubsystem *subsys,
                                         uint32_t grpid, uint8_t state)
{
    uint16_t cntlid;
    uint8_t old;

    old = subsys->ana[grpid].state;

    if (state == old) {
        return;
    }

    subsys->ana[grpid].state = state;

    for (cntlid = 0; cntlid < ARRAY_SIZE(subsys->ctrls); cntlid++) {
        if (!subsys->ctrls[cntlid]) {
            continue;
        }

        nvme_notice_event(subsys->ctrls[cntlid], NVME_AER_INFO_ANA_CHANGE);
    }
}

static const char *nvme_subsys_ana_states[] = {
    "",
    [NVME_ANA_STATE_OPTIMIZED]      = "optimized",
    [NVME_ANA_STATE_NON_OPTIMIZED]  = "non-optimized",
    [NVME_ANA_STATE_INACCESSIBLE]   = "inaccessible",
    [NVME_ANA_STATE_CHANGE]         = "change",
};

void hmp_nvme_ana_inject_state(Monitor *mon, const QDict *qdict)
{
    const char *id = qdict_get_str(qdict, "id");
    const uint32_t grpid = qdict_get_int(qdict, "grpid");
    const char *state = qdict_get_str(qdict, "state");
    NvmeSubsystem *subsys;
    DeviceState *dev;
    int i;

    dev = qdev_find_recursive(sysbus_get_default(), id);
    if (!dev) {
        monitor_printf(mon, "nvme-subsys(%s): invalid device id\n", id);
        return;
    }

    if (!grpid) {
        monitor_printf(mon, "nvme-subsys(%s): grpid should not be 0\n", id);
        return;
    }

    subsys = NVME_SUBSYS(dev);

    for (i = 0; i < ARRAY_SIZE(nvme_subsys_ana_states); i++) {
        if (!strcmp(nvme_subsys_ana_states[i], state)) {
            nvme_subsys_ana_state_change(subsys, grpid, i);
            monitor_printf(mon,
                           "nvme-subsys(%s): ANA state %s(%d) injected\n",
                           id, state, i);
            return;
        }
    }

    monitor_printf(mon, "nvme-subsys(%s): invalid state %s\n", id, state);
}

int nvme_subsys_register_ctrl(NvmeCtrl *n, Error **errp)
{
    NvmeSubsystem *subsys = n->subsys;
    int cntlid;

    for (cntlid = 0; cntlid < ARRAY_SIZE(subsys->ctrls); cntlid++) {
        if (!subsys->ctrls[cntlid]) {
            break;
        }
    }

    if (cntlid == ARRAY_SIZE(subsys->ctrls)) {
        error_setg(errp, "no more free controller id");
        return -1;
    }

    subsys->ctrls[cntlid] = n;

    return cntlid;
}

int nvme_subsys_register_ns(NvmeNamespace *ns, Error **errp)
{
    NvmeSubsystem *subsys = ns->subsys;
    NvmeAna *ana;
    NvmeCtrl *n;
    int i;

    if (subsys->namespaces[nvme_nsid(ns)]) {
        error_setg(errp, "namespace %d already registerd to subsy %s",
                   nvme_nsid(ns), subsys->parent_obj.id);
        return -1;
    }

    subsys->namespaces[nvme_nsid(ns)] = ns;

    for (i = 0; i < ARRAY_SIZE(subsys->ctrls); i++) {
        n = subsys->ctrls[i];

        if (n && nvme_register_namespace(n, ns, errp)) {
            return -1;
        }
    }

    if (ns->params.anagrpid) {
        ana = &subsys->ana[ns->params.anagrpid];

        nvme_subsys_ana_register_ns(ana, nvme_nsid(ns));
        ns->ana = ana;
    }

    return 0;
}

static void nvme_subsys_setup(NvmeSubsystem *subsys)
{
    uint32_t anagrpid;

    snprintf((char *)subsys->subnqn, sizeof(subsys->subnqn),
             "nqn.2019-08.org.qemu:%s", subsys->parent_obj.id);

    for (anagrpid = 1; anagrpid < ARRAY_SIZE(subsys->ana); anagrpid++) {
        subsys->ana[anagrpid].grpid = anagrpid;
        subsys->ana[anagrpid].state = NVME_ANA_STATE_OPTIMIZED;
    }
}

static void nvme_subsys_realize(DeviceState *dev, Error **errp)
{
    NvmeSubsystem *subsys = NVME_SUBSYS(dev);

    nvme_subsys_setup(subsys);
}

static Property nvme_subsys_props[] = {
    DEFINE_PROP_BOOL("ana", NvmeSubsystem, params.ana, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvme_subsys_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    dc->bus_type = TYPE_BUS;
    dc->realize = nvme_subsys_realize;
    dc->desc = "Virtual NVMe subsystem";
    device_class_set_props(dc, nvme_subsys_props);
}

static const TypeInfo nvme_subsys_info = {
    .name = TYPE_NVME_SUBSYS,
    .parent = TYPE_DEVICE,
    .class_init = nvme_subsys_class_init,
    .instance_size = sizeof(NvmeSubsystem),
};

static void nvme_subsys_register_types(void)
{
    type_register_static(&nvme_subsys_info);
}

type_init(nvme_subsys_register_types)
