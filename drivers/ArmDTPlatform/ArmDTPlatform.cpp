// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Generic AArch64 Darwin platform expert for a supplied Apple-format device
// tree. It derives from XNU's real IODTPlatformExpert and relies on the
// platform's own DT root name ("device-tree") for matching. No board name,
// SoC address or vendor identity is compiled into this source.
//
// The DT must contain the nodes XNU's IODT platform path consumes:
//   /chosen with a CHRP NVRAM proxy (nvram-bank-size, nvram-proxy-data)
//   /options, /cpus, /arm-io and /defaults
// The NVRAM proxy is required so the real IODTNVRAM is published before any
// security initialization. This driver does not fabricate NVRAM or suppress
// a missing one; absent inputs make configure() fail.

#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>

// Observation counters describe actual published nubs, not started drivers.
extern "C" {
volatile uint32_t armdt_platform_configured = 0;
volatile uint32_t armdt_platform_child_count = 0;
volatile uint32_t armdt_platform_cpu_count = 0;
volatile uint32_t armdt_platform_bus_count = 0;
volatile uint32_t armdt_platform_defaults_count = 0;
volatile uint32_t armdt_platform_nvram_proxy = 0;
volatile uint32_t armdt_platform_device_count = 0;
}

/*
 * The panic screen replaces the console, so kprintf history from configure()
 * never survives into the final panic.  Publish the last completed step as a
 * property on the root nub instead; IOPanicPlatform reads it back and folds
 * it into the panic text.  Stages:
 *   1 probe accepted, 2 configure entered, 3 top-level nodes present,
 *   4 NVRAM proxy present, 5 IODTPlatformExpert::configure returned true,
 *   6 arm-io device nubs published, 7 service registered.
 */
static void
armdt_mark_stage (IOService *provider, unsigned stage)
{
  if (provider == nullptr) {
    return;
  }
  OSNumber *number = OSNumber::withNumber ((unsigned long long)stage, 32);
  if (number != nullptr) {
    provider->setProperty ("yunluo-armdt-stage", number);
    number->release ();
  }
}

extern "C" void yunluo_paint_band (int band, unsigned int color);

class ArmDTPlatform : public IODTPlatformExpert {
  OSDeclareDefaultStructors (ArmDTPlatform)
public:
  IOService *probe (IOService *provider, SInt32 *score) override;
  bool configure (IOService *provider) override;
  const char *deleteList (void) override;
  const char *excludeList (void) override;
private:
  bool hasNVRAMProxy (IOService *provider);
  bool createDeviceNubs (IOService *provider);
};

OSDefineMetaClassAndStructors (ArmDTPlatform, IODTPlatformExpert)

/*
 * The kernel config table deliberately gives this driver no IONameMatch: the
 * ADT comes from DarwinBoot and its root name is not guaranteed to be
 * "device-tree".  The inherited IODTPlatformExpert::probe() would compare
 * against that absent property and reject the platform, leaving the root to
 * IOPanicPlatform.  Accept the platform expert device here instead; the real
 * validation is configure(), which refuses an ADT missing the nodes XNU
 * consumes.  Log the actual root identity so a serial-less boot still shows
 * which device tree the kernel received.
 */
IOService *ArmDTPlatform::probe (IOService *provider, SInt32 *score)
{
  yunluo_paint_band (11, 0xFF00FF80);   /* gray: ArmDTPlatform::probe */
  if (provider == nullptr) {
    return nullptr;
  }
  OSData *model = OSDynamicCast (OSData, provider->getProperty ("model"));
  OSData *compat = OSDynamicCast (OSData, provider->getProperty ("compatible"));
  const char *modelStr = model ? (const char *)model->getBytesNoCopy () : "(none)";
  const char *compatStr = compat ? (const char *)compat->getBytesNoCopy () : "(none)";
  kprintf ("YUNLUO-PLAT: probe root=%s model=%.48s compat=%.96s\n",
           provider->getName (), modelStr, compatStr);
  armdt_mark_stage (provider, 1);
  return this;
}

// Serialized empty name lists: XNU's processTopLevel() treats a null pointer
// as "every entry" (remove every child), not as "nothing". An empty list
// genuinely selects no entry, so the supplied device tree is preserved.
const char *ArmDTPlatform::deleteList (void)
{
  return "()";
}

const char *ArmDTPlatform::excludeList (void)
{
  return "()";
}

bool ArmDTPlatform::hasNVRAMProxy (IOService *provider)
{
  IORegistryEntry *chosen = provider->childFromPath ("chosen", gIODTPlane);
  if (chosen == nullptr) {
    return false;
  }
  OSData *bankSize = OSDynamicCast (OSData, chosen->getProperty ("nvram-bank-size"));
  OSData *proxyData = OSDynamicCast (OSData, chosen->getProperty ("nvram-proxy-data"));
  bool valid = (bankSize != nullptr) && (bankSize->getLength () == sizeof (uint32_t)) &&
               (proxyData != nullptr) && (proxyData->getLength () != 0);
  chosen->release ();
  return valid;
}

bool ArmDTPlatform::createDeviceNubs (IOService *provider)
{
  IORegistryEntry *bus = provider->childFromPath ("arm-io", gIODTPlane);
  if (bus == nullptr) {
    return false;
  }
  // IODTPlatformExpert's IOPlatformDevice nubs resolve resources through
  // their provider's IOPlatformExpert::getNubResources. They must therefore
  // be attached to this platform expert, not to the arm-io IOPlatformDevice
  // (ArmVirtPlatform uses the same parent relationship). The device-tree
  // parent remains arm-io, so reg/ranges resolution still follows the bus.
  // Publish every real arm-io child, not only the first pattern match. The
  // GIC lives here, and the UFS host/PHY nodes must be visible to their native
  // drivers too. processTopLevel() still uses deleteList()/excludeList().
  OSIterator *iterator = bus->getChildIterator (gIODTPlane);
  bus->release ();
  if (iterator == nullptr) {
    return false;
  }
  uint32_t count = 0;
  IORegistryEntry *entry;
  while ((entry = OSDynamicCast (IORegistryEntry, iterator->getNextObject ())) != nullptr) {
    IOService *nub = createNub (entry);
    if (nub == nullptr) {
      continue;
    }
    if (!nub->attach (this)) {
      nub->release ();
      continue;
    }
    nub->registerService ();
    nub->release ();
    count++;
  }
  iterator->release ();
  armdt_platform_device_count = count;
  return true;
}

static void platformStageReset (void)
{
  // PSCI SYSTEM_RESET via the same SMC conduit as the entry reset probe.
  register uint64_t x0 __asm__ ("x0") = 0x84000009;
  register uint64_t x1 __asm__ ("x1") = 0;
  register uint64_t x2 __asm__ ("x2") = 0;
  register uint64_t x3 __asm__ ("x3") = 0;
  __asm__ volatile ("smc #0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x3) : "memory");
  for (;;) { __asm__ volatile ("wfe"); }
}

bool ArmDTPlatform::configure (IOService *provider)
{
  yunluo_paint_band (12, 0xFFFF0080);   /* gray: ArmDTPlatform::configure */
  IORegistryEntry *cpus = provider->childFromPath ("cpus", gIODTPlane);
  IORegistryEntry *bus = provider->childFromPath ("arm-io", gIODTPlane);
  IORegistryEntry *defaults = provider->childFromPath ("defaults", gIODTPlane);
  IORegistryEntry *options = provider->childFromPath ("options", gIODTPlane);
  armdt_mark_stage (provider, 2);
  bool haveCpus = (cpus != nullptr);
  bool haveBus = (bus != nullptr);
  bool haveDefaults = (defaults != nullptr);
  bool haveOptions = (options != nullptr);
  bool structure = haveCpus && haveBus && haveDefaults && haveOptions;
  if (cpus != nullptr) { cpus->release (); }
  if (bus != nullptr) { bus->release (); }
  if (defaults != nullptr) { defaults->release (); }
  if (options != nullptr) { options->release (); }
  kprintf ("YUNLUO-PLAT: configure enter cpus=%d bus=%d defaults=%d options=%d\n",
           haveCpus, haveBus, haveDefaults, haveOptions);
  if (!structure) {
    IOLog ("ArmDTPlatform: required /cpus, /arm-io, /defaults or /options node is absent\n");
    return false;
  }
  armdt_mark_stage (provider, 3);
  if (!hasNVRAMProxy (provider)) {
    IOLog ("ArmDTPlatform: /chosen does not supply a usable CHRP NVRAM proxy\n");
    return false;
  }
  armdt_mark_stage (provider, 4);
  kprintf ("YUNLUO-PLAT: configure nvram proxy ok\n");
  unsigned platformStage = 0;
  PE_parse_boot_argn ("sheng-platform-stage", &platformStage, sizeof (platformStage));
  if (platformStage == 1) {
    platformStageReset ();
  }

  // The inherited implementation publishes real IODTNVRAM from /chosen,
  // then attaches the CPU and top-level nubs. No identity or resource is
  // invented here; failure is returned instead of replaced with a stub.
  if (!IODTPlatformExpert::configure (provider)) {
    kprintf ("YUNLUO-PLAT: configure super failed\n");
    return false;
  }
  armdt_mark_stage (provider, 5);
  kprintf ("YUNLUO-PLAT: configure super ok\n");
  if (platformStage == 2) {
    platformStageReset ();
  }

  OSIterator *iterator = getChildIterator (gIOServicePlane);
  if (iterator == nullptr) {
    return false;
  }
  uint32_t count = 0;
  uint32_t cpuCount = 0;
  uint32_t busCount = 0;
  uint32_t defaultsCount = 0;
  IORegistryEntry *child;
  while ((child = OSDynamicCast (IORegistryEntry, iterator->getNextObject ())) != nullptr) {
    count++;
    OSData *type = OSDynamicCast (OSData, child->getProperty ("device_type"));
    if ((type != nullptr) && (type->getLength () == 4) &&
        (memcmp (type->getBytesNoCopy (), "cpu", 4) == 0)) {
      cpuCount++;
    }
    if (strcmp (child->getName (), "arm-io") == 0) {
      busCount++;
    }
    if (strcmp (child->getName (), "defaults") == 0) {
      defaultsCount++;
    }
  }
  iterator->release ();
  armdt_platform_child_count = count;
  armdt_platform_cpu_count = cpuCount;
  armdt_platform_bus_count = busCount;
  armdt_platform_defaults_count = defaultsCount;
  armdt_platform_nvram_proxy = 1;
  armdt_platform_configured = 1;
  // Publish the bus child devices (interrupt controller, VirtIO MMIO, ...)
  // as real IOPlatformDevice nubs. No driver is started here.
  if (!createDeviceNubs (provider)) {
    kprintf ("YUNLUO-PLAT: configure createDeviceNubs failed\n");
    return false;
  }
  kprintf ("YUNLUO-PLAT: configure ok children=%u cpus=%u bus=%u defaults=%u devices=%u\n",
           count, cpuCount, busCount, defaultsCount, armdt_platform_device_count);
  armdt_mark_stage (provider, 6);
  if (platformStage == 3) {
    platformStageReset ();
  }
  // Explicit hardware A/B diagnostic: reach the end of the real platform
  // configuration and panic before publishing the service. The payload's
  // panic-screen probe makes this observable without a serial console.
  unsigned platformProbe = 0;
  PE_parse_boot_argn ("sheng-platform-panic", &platformProbe, sizeof (platformProbe));
  if (platformProbe != 0 || platformStage == 4) {
    platformStageReset ();
  }
  registerService ();
  yunluo_paint_band (13, 0xFF004080);   /* gray: platform registered */
  armdt_mark_stage (provider, 7);
  if (platformStage == 5) {
    platformStageReset ();
  }
  return true;
}
