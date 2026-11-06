// SPDX-License-Identifier: BSD-2-Clause-Patent
// Keyboard/consumer/digitizer reports through the selected IOHIDDevice interface.
// Source-interface compilation does not qualify a target family's binary ABI.
#if defined(__arm64e__) && !(defined(NATIVE_HID_SOURCE_API) && NATIVE_HID_SOURCE_API)
#error "The original HID ABI path is arm64 iOS 17 only; use the source API build for arm64e."
#endif
#if defined(NATIVE_HID_SOURCE_API) && NATIVE_HID_SOURCE_API
#include <IOKit/hid/IOHIDDevice.h>
#else
#include "OriginalHidDeviceABI.h"
#endif
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOLocks.h>
#include "HidReportState.h"
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSIterator.h>
#include <pexpert/pexpert.h>
#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif
extern "C" void native_hid_input_observed(unsigned,int);
extern "C" void native_hid_live_checkpoint(IOService *);
extern "C" void native_hid_queue_checkpoint(IOService *,const char *);
// Publishes the usage class backboardd reads from the HID event service: the digitizer needs
// (0x0D,4) and the consumer control device needs (0x0C,1). Without it the events are dropped
// as coming from an unsupported service.
extern "C" void native_hid_publish_service_class(IOService *,unsigned long long,unsigned long long,unsigned char);
extern "C" bool native_hid_input_requested(void);
extern "C" void native_touch_set_sink(void (*)(void *), void *) __attribute__((weak_import));
// Optional hardware buttons fed by the board button driver: a mask of report 2 bits, sent on every change.
extern "C" void native_buttons_set_sink(void (*)(void *, uint32_t), void *) __attribute__((weak_import));
// Optional touch digitizer fed by the board touch probe (kits/sheng/TouchProbe.cpp): finger set published with a seqlock.
extern "C" {
extern volatile uint32_t native_touch_seq, native_touch_count, native_touch_hid_reports;
extern volatile int native_touch_xs[12], native_touch_ys[12], native_touch_ids[12];
// Producer units: the sheng touch core works in 0.1 px of a 3048 x 2032 panel, the yunluo probe
// publishes panel pixels of a 1200 x 2000 one. The defaults keep sheng's range.
extern volatile int native_touch_max_x, native_touch_max_y;
extern volatile uint32_t native_touch_opened;
extern volatile unsigned native_hid_auto_bit;
}
extern "C" {
volatile int native_touch_max_x = 30480, native_touch_max_y = 20320;
}
// Power and volume keys arrive as consumer usages from the board key probe and are published as
// individual report 2 bits, the arrangement the sheng port qualified on hardware.
extern "C" void native_hid_input_key(unsigned short usage, int down);
// Keyboard Application collection ensures the original generic keyboard
// personality can match. Report 2 carries the Consumer buttons, bit 0 Menu (Home),
// bit 1 Power, bit 2 Volume Increment, bit 3 Volume Decrement. Report 1 is an
// ordinary boot keyboard report.
static const unsigned char descriptor[]={
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,
    0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x08,0x81,0x02,
    0x75,0x08,0x95,0x01,0x81,0x03,
    0x19,0x00,0x29,0x65,0x15,0x00,0x25,0x65,
    0x75,0x08,0x95,0x06,0x81,0x00,
    0x85,0x02,0x05,0x0c,0x09,0x40,0x09,0x30,0x09,0xe9,0x09,0xea,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x04,0x81,0x02,
    0x75,0x04,0x95,0x01,0x81,0x03,0xc0
};
static const unsigned char consumerDescriptor[]={
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x02,
    0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,
    0x09,0x40,0x81,0x02,
    0x09,0x30,0x81,0x02,
    0x09,0xe9,0x81,0x02,
    0x09,0xea,0x81,0x02,
    0x75,0x04,0x95,0x01,0x81,0x03,0xc0
};

// Report 3: ten-finger Touch Screen collection (Tip Switch, Touch Valid, Contact Identifier, X, Y per finger, then Contact Count).
// Touch Valid (0x47) is the finger confidence: IOHIDEventDriver turns a finger transducer whose value is 0 into a hand (palm), which
// multi-finger and system edge gestures ignore, so every finger, down or lifting, sends 1.
// Coordinates are normalised to 0..32767 on both axes by the producer.
#define FINGER_DESC \
    0x09,0x22,0xa1,0x02, 0x09,0x42,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x81,0x02, 0x09,0x47,0x81,0x02, 0x75,0x06,0x95,0x01,0x81,0x03, \
    0x09,0x51,0x75,0x08,0x95,0x01,0x15,0x00,0x26,0xff,0x00,0x81,0x02, \
    0x05,0x01,0x09,0x30,0x15,0x00,0x26,0xff,0x7f,0x75,0x10,0x95,0x01,0x81,0x02, 0x09,0x31,0x81,0x02, 0x05,0x0d, 0xc0,
static const unsigned char touchDescriptor[]={
    0x05,0x0d,0x09,0x04,0xa1,0x01,0x85,0x03,
    FINGER_DESC FINGER_DESC FINGER_DESC FINGER_DESC FINGER_DESC
    FINGER_DESC FINGER_DESC FINGER_DESC FINGER_DESC FINGER_DESC
    0x09,0x54,0x75,0x08,0x95,0x01,0x15,0x00,0x26,0xff,0x00,0x81,0x02,0xc0,
    // Second top-level collection: fb207's boot log shows backboardd registering an IOHIDDevice
    // first-match with DeviceUsagePairs {DeviceUsagePage 0xff00, DeviceUsage 0x02}, which the
    // plain Digitizer/Touch Screen pair never satisfies -- the touch service was never matched.
    // Its own report id keeps it out of the digitizer report space; we never send report 4 here.
    0x06,0x00,0xff,0x09,0x02,0xa1,0x01,0x85,0x04,
    0x75,0x08,0x95,0x01,0x81,0x03,0xc0,
    // Third collection, and the reason the digitizer can get an event driver at all: no IOHIDEventDriver
    // personality in this kernel matches (0x0d,*), the only one that accepts a panel is Apple's own
    // "Peppy Touch Display" (DeviceUsagePairs 0x0c/0x01 + Transport=FIFO, kernel.macho __PRELINK_INFO).
    // Declaring the Consumer Control pair makes that personality bind here; the classification the
    // system uses stays the digitizer because it is the FIRST collection and because handleOpen
    // publishes PrimaryUsagePage/Usage 0x0d/0x04 on the driver. Report 5 is padding, never sent.
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x05,
    0x75,0x08,0x95,0x01,0x81,0x03,0xc0
};
#define TOUCH_FINGERS 10
// Edge contacts (int-163). A contact that starts within ~3 mm of a panel edge carries kIOHIDDigitizerEventFromEdgeTip (0x800) in its
// finger events' event mask for its whole life, as Apple's multitouch stack reports it (MultitouchHID). UIKit derives UITouch's edge
// type from that bit alone (_UIEventHIDTouchEdgeTypeForHIDEvent: 0x800/0x1000/0x2000/0x40000 -> 1..4), and every screen-edge
// recognizer of SpringBoard fails at once on edge type 0: int-162 logged edge=0 for touches starting 2 pt above the bottom edge and
// SBHomeGesturePanGestureRecognizer / SBFluidSwitcherScreenEdgePanGestureRecognizer going 0->5, so no home or switcher gesture.
// The original IOHIDEventDriver (IOHIDFamily-2031.120.4) never sets the bit. Its dispatchEvent (IOHIDEventService slot 198, the slot
// 37 of IOHIDFamily's indirect calls use) is wrapped for this device's driver only, in a copy of its vtable; the wrapper adds the bit
// to the finger children of each digitizer collection and calls the original. IOHIDEvent virtuals of that version: getType 18,
// getIntegerValue 23, setIntegerValue 24, getFixedValue 25 (setIntegerValue/getIntegerValue at +0xc0/+0xb8 in the driver's own
// calls); _children at +24 (IOHIDEvent.h; appendChild stores into it and free releases it). _parent is never set (appendChild only
// stores the child, int-163 skipped every finger by comparing it), so each child is an IOHIDEvent by its class instead.
namespace edge {
#if defined(NATIVE_HID_SOURCE_API) && NATIVE_HID_SOURCE_API && !__has_feature(ptrauth_calls)
// Source-only bring-up without pointer authentication emits ordinary digitizer reports: a copied vtable of another family's ABI is never used.
extern "C" {volatile uint32_t native_touch_hid_multi[3];}
extern "C" {volatile uint32_t native_touch_edge_stats[4];} // installed stays zero
static void install(IORegistryEntry *) {}
#else
enum {kSlots=256,kDispatch=198,kGetType=18,kGetInteger=23,kSetInteger=24,kGetFixed=25,kContacts=256};
enum : uint32_t {kDigitizer=11,kX=0xB0000,kY=0xB0001,kIndex=0xB0005,kMask=0xB0007,kTouch=0xB0009,kMaskTouch=2,kFromEdgeTip=0x800};
// 48 panel pixels of 3048 x 2032 (the sweet reference uses 48 px), in the driver's normalised IOFixed coordinates.
static const int32_t kEdgeX=48*65536/3048,kEdgeY=48*65536/2032;
#if __has_feature(ptrauth_calls)
static void (*originalDispatch)(IOService *,OSObject *,IOOptionBits);
#else
static uintptr_t originalDispatch;
#endif
static bool fromEdge[kContacts];
extern "C" {volatile uint32_t native_touch_edge_stats[4];}   // [0] installed, [1] contacts begun, [2] of them at an edge, [3] skipped events
// [0] reports with two or more fingers down, [1] the most fingers down in one report, [2] tip-up entries sent for vanished fingers
extern "C" {volatile uint32_t native_touch_hid_multi[3];}
#if __has_feature(ptrauth_calls)
// arm64e (iPadOS 27): an object's vtable pointer is signed with the DA key (address of the object, discriminator 0xcda1 of OSMetaClassBase),
// each slot with the IA key (address of the slot, the discriminator of the virtual function). IOHIDFamily-2238.120.5 keeps the slot numbers
// above; the discriminators are those of 24A446's vtables.
enum : uint16_t {kDivDispatch=0x1134,kDivGetType=0x8758,kDivGetInteger=0x02c5,kDivSetInteger=0x130e,kDivGetFixed=0xf637,kVptrDiscriminator=0xcda1};
static void **vtableOf(void *object) {
    return reinterpret_cast<void **>(ptrauth_auth_data(*reinterpret_cast<void **>(object),ptrauth_key_process_independent_data,
        ptrauth_blend_discriminator(object,kVptrDiscriminator)));
}
template <typename R,typename... A> static R call(void *object,unsigned slot,uint16_t diversity,A... args) {
    void **entry=vtableOf(object)+slot;
    void *function=ptrauth_auth_and_resign(*entry,ptrauth_key_function_pointer,ptrauth_blend_discriminator(entry,diversity),
        ptrauth_key_function_pointer,0);
    return reinterpret_cast<R (*)(void *,A...)>(function)(object,args...);
}
#else
template <typename R,typename... A> static R call(void *object,unsigned slot,uint16_t,A... args) {
    return reinterpret_cast<R (*)(void *,A...)>((*reinterpret_cast<uintptr_t **>(object))[slot])(object,args...);
}
enum : uint16_t {kDivDispatch=0,kDivGetType=0,kDivGetInteger=0,kDivSetInteger=0,kDivGetFixed=0};
#endif
// One finger event; true when it now carries the bit.
static bool mark(void *child) {
    if(call<uint32_t,IOOptionBits>(child,kGetType,kDivGetType,0)!=kDigitizer)return false;   // the scan-time vendor child
    const uint32_t index=(uint32_t)call<int32_t,uint32_t,IOOptionBits>(child,kGetInteger,kDivGetInteger,kIndex,0);
    if(index>=kContacts) {native_touch_edge_stats[3]=native_touch_edge_stats[3]+1;return false;}
    const uint32_t mask=(uint32_t)call<int32_t,uint32_t,IOOptionBits>(child,kGetInteger,kDivGetInteger,kMask,0);
    const bool touch=call<int32_t,uint32_t,IOOptionBits>(child,kGetInteger,kDivGetInteger,kTouch,0)!=0;
    if((mask&kMaskTouch) && touch) {   // the contact begins: its position now decides
        const int32_t x=call<int32_t,uint32_t,IOOptionBits>(child,kGetFixed,kDivGetFixed,kX,0),y=call<int32_t,uint32_t,IOOptionBits>(child,kGetFixed,kDivGetFixed,kY,0);
        fromEdge[index]=x<kEdgeX || x>65536-kEdgeX || y<kEdgeY || y>65536-kEdgeY;
        native_touch_edge_stats[1]=native_touch_edge_stats[1]+1;
        if(fromEdge[index])native_touch_edge_stats[2]=native_touch_edge_stats[2]+1;
    }
    const bool edged=fromEdge[index];
    if(edged)call<void,uint32_t,int32_t,IOOptionBits>(child,kSetInteger,kDivSetInteger,kMask,(int32_t)(mask|kFromEdgeTip),0);
    if(!touch && (mask&kMaskTouch))fromEdge[index]=false;   // the lift keeps the bit; the next contact with this index starts fresh
    return edged;
}
static void dispatch(IOService *self,OSObject *event,IOOptionBits options) {
    if(event && call<uint32_t,IOOptionBits>(event,kGetType,kDivGetType,0)==kDigitizer) {
        OSArray *children=*reinterpret_cast<OSArray **>(reinterpret_cast<uint8_t *>(event)+24);
        if(children && OSDynamicCast(OSArray,children) && children->getCount()) {
            // A collection (the driver's touch-screen dispatch): its mask is the union of its fingers', so it takes the bit as well.
            bool any=false;
            for(unsigned i=0;i<children->getCount();++i) {
                OSObject *child=children->getObject(i);
                if(child && child->metaCast("IOHIDEvent"))any|=mark(child);
                else native_touch_edge_stats[3]=native_touch_edge_stats[3]+1;
            }
            if(any) {
                const uint32_t mask=(uint32_t)call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,kDivGetInteger,kMask,0);
                call<void,uint32_t,int32_t,IOOptionBits>(event,kSetInteger,kDivSetInteger,kMask,(int32_t)(mask|kFromEdgeTip),0);
            }
        } else if(!children) mark(event);   // one transducer dispatched on its own
    }
#if __has_feature(ptrauth_calls)
    originalDispatch(self,event,options);
#else
    reinterpret_cast<void (*)(IOService *,OSObject *,IOOptionBits)>(originalDispatch)(self,event,options);
#endif
}
// This device's own event driver gets the wrapped copy; any other driver keeps the original table.
#if __has_feature(ptrauth_calls)
// The copy's slots are signed for their new addresses, each with its own discriminator. Those are Apple's: the diversity of each slot's
// authenticated fixup in 24A446's kernelcache (IOHIDEventDriver's vtable, 220 slots, IOHIDFamily-2238.120.5; ipsw kernel cpp --methods).
// They are not searched for: a kernel pointer's PAC has about 24 bits, so among 65536 discriminators a slot often has a second one whose
// signature for the old address is the same, and the first match found was then wrong for the copy (slot 214, 0xcca1: a PAC failure in
// IOHIDFamily and dead touch, untagged-31ec7a92ba). Every slot is checked against the original table before the copy is used; a table that
// is not this one (another OS build) is left alone. [0] the outcome (1 another class, 2 a slot that did not check out, 3 installed), [1]
// the number of slots copied (0 = not installed), [2] the first slot that did not check out (kSlots: none).
extern "C" {volatile uint32_t native_touch_edge_install[3];}
static const uint16_t kSlotDiversity[]={
    0x3771,0x56b7,0xbaa2,0x3607,0x2e4a,0x3a87,0x89a9,0xfffc,0xfc74,0x5635,0xbe60,0x32e5,
    0x9209,0x4a6a,0xedc5,0x5c68,0x6a10,0x7a2a,0xaf75,0x137e,0x0655,0x43aa,0x12e9,0x4578,
    0x4275,0xff53,0x1814,0x122e,0x13f6,0x1d35,0xacb1,0x7eb0,0x1262,0x82eb,0x164e,0x37a5,
    0xb659,0x6c51,0xa20f,0xb3b6,0x6bcb,0x5a20,0x5062,0x00d7,0x7c85,0x8a26,0x3539,0x688b,
    0x1e60,0x1955,0x0689,0xc256,0xa383,0xf021,0x1f0a,0xb4bb,0x8ffc,0xb5b9,0x8764,0x5d96,
    0x80d9,0x0c9c,0x5d0a,0xcbcc,0x617d,0x848a,0x2312,0x3540,0xc257,0x3025,0x9fc2,0x5038,
    0xc666,0x6cc3,0x550c,0xa19a,0xa51b,0x4577,0x573c,0x1a4e,0x6c3d,0xb049,0xc4b2,0xc90d,
    0x7d59,0x4897,0x3c68,0xb085,0x4529,0x639f,0xccfb,0x55eb,0xe933,0xaec3,0x5ec5,0x5219,
    0xc6b2,0x8a43,0x4a20,0xd9f2,0x981a,0xa27f,0xc4f9,0x6b87,0x60a1,0x7e78,0x36aa,0x86ef,
    0x9be9,0x7318,0x93b7,0x638e,0x61a6,0x9175,0x136b,0xdb58,0x4a31,0x0988,0x5393,0xabe0,
    0x0ad9,0x6c99,0xd52d,0xe213,0x308f,0xd78d,0x3a1d,0xa390,0x240b,0x1b89,0x8d3c,0x2652,
    0x7f14,0x0759,0x63c4,0x800f,0x9cc2,0x02ac,0x785f,0xcc6b,0x82cd,0x808e,0x37ce,0xa4c7,
    0xe8de,0xa343,0x4bc0,0xf8a6,0xac7f,0x7974,0xea1b,0x4b35,0x9eb4,0x595a,0x5b2b,0x699e,
    0x2b52,0xf40e,0x0ddb,0x0f88,0x8700,0x36c3,0x058e,0xf16e,0x3a71,0xda1e,0x10b6,0x8654,
    0x96c3,0x4cb5,0x19b8,0x6518,0x5717,0xd2bf,0x09fa,0xad91,0xd335,0x4902,0xae57,0xaad9,
    0x5299,0xe604,0x8660,0x0189,0xc3de,0x41f6,0x5c1e,0xb54e,0xef25,0x4bdd,0xefa8,0x9835,
    0xae71,0x67cf,0xa85e,0x7b4f,0x4abb,0xc0d0,0x1134,0x7eca,0xf128,0xc748,0xa49a,0xde1d,
    0x90ba,0xea29,0xce88,0xc62d,0x0078,0x0dce,0x454b,0xc58b,0xe23b,0x2684,0xcca1,0xfe11,
    0x69fc,0xaaca,0x6518,0x7547,
};
static constexpr unsigned kDriverSlots=sizeof(kSlotDiversity)/sizeof(kSlotDiversity[0]);
static_assert(kDriverSlots>kDispatch && kDriverSlots<=kSlots,"the dispatch slot lies in the table");
static void install(IORegistryEntry *driver) {
    if(originalDispatch || !driver)return;
    if(kSlotDiversity[kDispatch]!=kDivDispatch)return;
    // The table fits these classes only, each checked in 24A446's kernelcache to have exactly these 220 slots with these discriminators:
    // IOHIDEventDriver, and AppleUserHIDEventService, its subclass that drives this device (the user-space HID driver's service). Any
    // other subclass may have slots past the table, which a copy of this size would lose.
    const OSMetaClass *meta=driver->getMetaClass();
    const char *name=meta?meta->getClassName():nullptr;
    if(!name || (strcmp(name,"IOHIDEventDriver") && strcmp(name,"AppleUserHIDEventService"))) {native_touch_edge_install[0]=1;return;}
    void **table=vtableOf(driver);
    void **copy=static_cast<void **>(IOMalloc(kDriverSlots*sizeof(void *)));
    if(!copy)return;
    unsigned copied=0;
    native_touch_edge_install[2]=kSlots;
    for(unsigned i=0;i<kDriverSlots;++i) {
        void *entry=table[i];
        void *function=entry?ptrauth_strip(entry,ptrauth_key_function_pointer):nullptr;
        if(!entry || ptrauth_sign_unauthenticated(function,ptrauth_key_function_pointer,
               ptrauth_blend_discriminator(&table[i],kSlotDiversity[i]))!=entry) {
            native_touch_edge_install[2]=i;copied=0;break;
        }
        if(i==kDispatch) {
            originalDispatch=reinterpret_cast<void (*)(IOService *,OSObject *,IOOptionBits)>(
                ptrauth_sign_unauthenticated(function,ptrauth_key_function_pointer,0));
            function=ptrauth_strip(reinterpret_cast<void *>(&dispatch),ptrauth_key_function_pointer);
        }
        copy[i]=ptrauth_sign_unauthenticated(function,ptrauth_key_function_pointer,ptrauth_blend_discriminator(&copy[i],kSlotDiversity[i]));
        copied=i+1;
    }
    native_touch_edge_install[1]=copied;
    if(copied!=kDriverSlots || !originalDispatch) {
        native_touch_edge_install[0]=2;originalDispatch=nullptr;IOFree(copy,kDriverSlots*sizeof(void *));return;
    }
    native_touch_edge_install[0]=3;
    __asm__ volatile("dmb ish":::"memory");
    *reinterpret_cast<void **>(driver)=ptrauth_sign_unauthenticated(copy,ptrauth_key_process_independent_data,
        ptrauth_blend_discriminator(driver,kVptrDiscriminator));
    native_touch_edge_stats[0]=1;
}
#else
static void install(IORegistryEntry *driver) {
    if(originalDispatch || !driver)return;
    uintptr_t *table=*reinterpret_cast<uintptr_t **>(driver);
    uintptr_t *copy=static_cast<uintptr_t *>(IOMalloc(kSlots*sizeof(uintptr_t)));
    if(!copy)return;
    memcpy(copy,table,kSlots*sizeof(uintptr_t));
    originalDispatch=copy[kDispatch];copy[kDispatch]=(uintptr_t)&dispatch;
    __asm__ volatile("dmb ish":::"memory");
    *reinterpret_cast<uintptr_t **>(driver)=copy;
    native_touch_edge_stats[0]=1;
}
#endif
#endif
}  // namespace edge
// Read-only probe of the keyboard driver's event dispatch (IOHIDEventService slot 198). fb226
// proved the service is opened by the userspace stack; this tells whether the driver turns our
// report 2 into keyboard events at all.
namespace kprobe {
constexpr unsigned kSlots=256,kDispatch=198,kGetType=18,kGetInteger=23;
static uintptr_t original;
static uint32_t logged;
template <typename R,typename... A> static R call(void *object,unsigned slot,A... args) {
    return reinterpret_cast<R (*)(void *,A...)>((*reinterpret_cast<uintptr_t **>(object))[slot])(object,args...);
}
static void dispatch(IOService *self,OSObject *event,IOOptionBits options) {
    bool sample=false;
    if(event && logged<24) {
        const uint32_t type=call<uint32_t,IOOptionBits>(event,kGetType,0);
        if(type==3) {
            sample=true;
            const int32_t page=call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,0x30000,0);
            const int32_t usage=call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,0x30001,0);
            const int32_t down=call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,0x30002,0);
            ++logged;
            kprintf("YUNLUO-INPUT kevent type=%u page=%x usage=%x down=%d\n",type,page,usage,down);
        } else {
            ++logged;
            kprintf("YUNLUO-INPUT kevent type=%u\n",type);
        }
    }
    reinterpret_cast<void (*)(IOService *,OSObject *,IOOptionBits)>(original)(self,event,options);
    if(sample)native_hid_queue_checkpoint(self,"key-event");
}
static void install(IORegistryEntry *driver) {
    if(original || !driver)return;
    uintptr_t *table=*reinterpret_cast<uintptr_t **>(driver);
    uintptr_t *copy=static_cast<uintptr_t *>(IOMalloc(kSlots*sizeof(uintptr_t)));
    if(!copy)return;
    memcpy(copy,table,kSlots*sizeof(uintptr_t));
    original=copy[kDispatch];copy[kDispatch]=(uintptr_t)&dispatch;
    __asm__ volatile("dmb ish":::"memory");
    *reinterpret_cast<uintptr_t **>(driver)=copy;
    kprintf("YUNLUO-INPUT kprobe installed\n");
}
}  // namespace kprobe
// Board key queue: filled from a kernel thread, published from this device's work loop as report 2.
namespace keys {
static IOLock *lock;
static hid_report::KeyQueue queue;
IOService *device;
unsigned char mask;
bool prepare() {
    if(!lock)lock=IOLockAlloc();
    return lock!=nullptr;
}
// BKKeyboardInfo only admits a service whose primary usage is page 1/usage 6 (or one of two Apple
// special cases); a pure Consumer Control (0x0c,1) service is rejected, so the volume/power bits
// must travel on the keyboard service. The separate consumer device stays observation-only unless
// yunluo-key-consumer=1 is passed.
static bool consumerSink() {
    static int selected=-1;
    if(selected<0) {
        unsigned value=0;
        PE_parse_boot_argn("yunluo-key-consumer",&value,sizeof value);
        selected=value==1?1:0;
    }
    return selected!=0;
}
void attach(IOService *owner,bool consumer) {
    if(!lock)return;
    IOLockLock(lock);
    if(!device || (consumer && consumerSink()))device=owner;
    IOLockUnlock(lock);
}
void detach(IOService *owner) {
    if(!lock)return;
    IOLockLock(lock);
    if(device==owner)device=nullptr;
    IOLockUnlock(lock);
}
// One report 2 per change: the four Consumer variables share a single byte, so the bitmask is the
// whole state (a held key stays set, a release clears its bit).
// Report accounting for the board probe's log: the shared observer array is only eight entries wide,
// so a rejected key report would otherwise be invisible.
extern "C" {
volatile uint32_t native_key_reports;
volatile int32_t native_key_result;
volatile uint32_t native_key_mask;
volatile uint64_t native_key_object;
volatile uint32_t native_key_overflows;
}
void send() {
    unsigned char report[2]={2,mask};
    IOBufferMemoryDescriptor *buffer=IOBufferMemoryDescriptor::withBytes(report,sizeof report,kIODirectionNone);
    IOReturn result=buffer?static_cast<IOHIDDevice *>(device)->handleReport(buffer,0,0):kIOReturnNoMemory;
    OSSafeReleaseNULL(buffer);
    native_key_reports=native_key_reports+1;
    native_key_result=(int32_t)result;
    native_key_mask=mask;
    native_key_object=(uint64_t)device;
    if(native_key_reports<=200)
        kprintf("YUNLUO-INPUT key report n=%u mask=%02x result=%d object=%llx\n",
                (unsigned)native_key_reports,(unsigned)mask,(int)native_key_result,
                (unsigned long long)native_key_object);
}
void setBit(unsigned char bit,int down) {
    if(!lock)return;
    IOLockLock(lock);
    queue.push(bit,down!=0);
    native_key_overflows=queue.overflows;
    IOLockUnlock(lock);
}
void deliver(OSObject *owner,IOTimerEventSource *source) {
    source->setTimeoutMS(10);
    if(!lock)return;
    IOLockLock(lock);
    if(owner==device) {
        while(queue.pop(mask))send();
    }
    IOLockUnlock(lock);
}
unsigned char bitFor(unsigned short usage) {
    return hid_report::keyBit(usage);
}
}
namespace keys {void attach(IOService *,bool);void deliver(OSObject *,IOTimerEventSource *);void setBit(unsigned char,int);}
class NativeHidInput : public IOHIDDevice {
    OSDeclareDefaultStructors(NativeHidInput)
public:
    void setTouchOnly() {touchOnly=true;}
    void setConsumerOnly() {consumerOnly=true;}
    IOWorkLoop *inputLoop=nullptr;
    IOTimerEventSource *timer=nullptr;
    IOTimerEventSource *touchTimer=nullptr;
    IOTimerEventSource *keyTimer=nullptr;
    bool down=false;
    uint8_t buttons=0;   // report 2 bits held by the hardware buttons
    bool touchEnabled=false;
    bool touchRotate180=false;
    // The digitizer is a separate service from the keyboard: backboardd matches digitizers by primary usage page/usage
    // (Digitizer/Touch Screen) and the Built-In flag, which a combined keyboard-first descriptor would not provide.
    bool touchOnly=false;
    bool consumerOnly=false;
    unsigned kickTicks=0;
    unsigned lastOpen=0;
    bool eventDriverFound=false;
    IOReturn submitReport(IOMemoryDescriptor *report) {
#if defined(NATIVE_HID_SOURCE_API) && NATIVE_HID_SOURCE_API
        return handleReport(report,kIOHIDReportTypeInput,0);
#else
        return handleReport(report,0,0);
#endif
    }
    uint32_t lastSeq=0;
    // Fingers sent as down in the previous report; a vanished identifier gets one explicit tip-up report.
    int prevIds[TOUCH_FINGERS]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    int prevX[TOUCH_FINGERS]={},prevY[TOUCH_FINGERS]={};
    static unsigned transformCoordinate(int value,int maximum) {
        if(value<0)value=0;if(value>=maximum)value=maximum-1;
        return (unsigned)((long long)value*32767/(maximum-1));
    }
    // One finger set to the event driver. The touch thread (kits/sheng/TouchProbe.cpp) calls touchNotify after it publishes a frame; there is no timer
    // watching the seqlock any more (it woke the work loop every 8 ms for nothing).
    static void touchNotify(void *context) {
        NativeHidInput *self=static_cast<NativeHidInput *>(context);
        if(self && self->inputLoop)self->inputLoop->runAction(&NativeHidInput::touchAction,self);
    }
    static IOReturn touchAction(OSObject *owner,void *,void *,void *,void *) {
        NativeHidInput *self=OSDynamicCast(NativeHidInput,owner);if(!self)return kIOReturnSuccess;
        self->touchSend();
        return kIOReturnSuccess;
    }
    void touchSend() {
        NativeHidInput *self=this;
        if(!self->hasEventDriverCached())return;
        uint32_t before=native_touch_seq;
        if((before&1) || before==self->lastSeq)return;
        __asm__ volatile("dmb ish":::"memory");
        unsigned count=native_touch_count;if(count>TOUCH_FINGERS)count=TOUCH_FINGERS;
        hid_report::Contact contacts[TOUCH_FINGERS];
        for(unsigned i=0;i<count;++i)
            contacts[i]={native_touch_ids[i],native_touch_xs[i],native_touch_ys[i]};
        const int maxX=native_touch_max_x,maxY=native_touch_max_y;
        __asm__ volatile("dmb ish":::"memory");
        if(native_touch_seq!=before)return;      // torn read: retry on the next tick
        self->lastSeq=before;
        // Normalize the K70U producer axes to the upstream HID 0..32767 descriptor.
        unsigned char report[1+TOUCH_FINGERS*6+1]={3};
        unsigned slot=0;
        int nextIds[TOUCH_FINGERS],nextX[TOUCH_FINGERS],nextY[TOUCH_FINGERS];
        for(unsigned i=0;i<count && slot<TOUCH_FINGERS;++i,++slot) {
            const int rawX=touchRotate180?maxX-1-contacts[i].x:contacts[i].x;
            const int rawY=touchRotate180?maxY-1-contacts[i].y:contacts[i].y;
            unsigned x=transformCoordinate(rawX,maxX),y=transformCoordinate(rawY,maxY);
            unsigned char *f=report+1+slot*6;
            f[0]=3;f[1]=(unsigned char)(contacts[i].id&0xff);f[2]=(unsigned char)x;f[3]=(unsigned char)(x>>8);f[4]=(unsigned char)y;f[5]=(unsigned char)(y>>8);   // tip, valid
            nextIds[slot]=contacts[i].id;nextX[slot]=(int)x;nextY[slot]=(int)y;
        }
        const unsigned downCount=slot;
        if(downCount>=2)edge::native_touch_hid_multi[0]=edge::native_touch_hid_multi[0]+1;
        if(downCount>edge::native_touch_hid_multi[1])edge::native_touch_hid_multi[1]=downCount;
        // Identifiers that were down and are gone: tip up at the last known position. They are matched by identifier, not by slot:
        // the producer packs its fingers, so when one lifts the others move down a slot and a slot match would lift a finger still
        // down and leave the lifted one stuck.
        for(unsigned j=0;j<TOUCH_FINGERS;++j) {
            const int id=self->prevIds[j];
            if(id<0)continue;
            bool kept=false;
            for(unsigned i=0;i<downCount && !kept;++i)kept=(contacts[i].id&0xff)==(id&0xff);
            if(kept || slot>=TOUCH_FINGERS)continue;
            unsigned char *f=report+1+slot*6;
            f[0]=2;f[1]=(unsigned char)(id&0xff);   // tip up, valid
            f[2]=(unsigned char)self->prevX[j];f[3]=(unsigned char)(self->prevX[j]>>8);
            f[4]=(unsigned char)self->prevY[j];f[5]=(unsigned char)(self->prevY[j]>>8);
            ++slot;edge::native_touch_hid_multi[2]=edge::native_touch_hid_multi[2]+1;
        }
        for(unsigned j=0;j<TOUCH_FINGERS;++j) {
            self->prevIds[j]=j<downCount?nextIds[j]:-1;
            if(j<downCount){self->prevX[j]=nextX[j];self->prevY[j]=nextY[j];}
        }
        if(!slot)return;
        report[1+TOUCH_FINGERS*6]=(unsigned char)slot;
        IOBufferMemoryDescriptor *buffer=IOBufferMemoryDescriptor::withBytes(report,sizeof(report),kIODirectionNone);
        IOReturn result=buffer?self->submitReport(buffer):kIOReturnNoMemory;
        OSSafeReleaseNULL(buffer);
        native_hid_input_observed(6,result);
        native_touch_hid_reports=native_touch_hid_reports+1;
    }
    // The button driver's thread calls buttonsNotify on every change of the pressed set.
    static void buttonsNotify(void *context,uint32_t mask) {
        NativeHidInput *self=static_cast<NativeHidInput *>(context);
        if(self && self->inputLoop)self->inputLoop->runAction(&NativeHidInput::buttonsAction,self,reinterpret_cast<void *>(uintptr_t(mask)));
    }
    static IOReturn buttonsAction(OSObject *owner,void *mask,void *,void *,void *) {
        NativeHidInput *self=OSDynamicCast(NativeHidInput,owner);if(!self)return kIOReturnSuccess;
        self->buttons=static_cast<uint8_t>(uintptr_t(mask)&0xe);
        if(!self->hasEventDriverCached())return kIOReturnSuccess;
        unsigned char report[]={2,static_cast<unsigned char>((self->down?1:0)|self->buttons)};
        IOBufferMemoryDescriptor *buffer=IOBufferMemoryDescriptor::withBytes(report,sizeof(report),kIODirectionNone);
        IOReturn result=buffer?self->submitReport(buffer):kIOReturnNoMemory;
        OSSafeReleaseNULL(buffer);
        native_hid_input_observed(4,result);
        return kIOReturnSuccess;
    }
    bool hasEventDriverCached() {
        if(!eventDriverFound) {
            IORegistryEntry *driver=eventDriver(this,4);
            eventDriverFound=driver!=nullptr;
            if(driver && (touchEnabled || touchOnly))edge::install(driver);
        }
        return eventDriverFound;
    }
    static bool hasEventDriver(IORegistryEntry *parent,unsigned depth) {return eventDriver(parent,depth)!=nullptr;}
    // The IOHIDEventDriver below this device, if one has attached.
    static IORegistryEntry *eventDriver(IORegistryEntry *parent,unsigned depth) {
        if(!depth)return nullptr;
        OSIterator *it=parent->getChildIterator(gIOServicePlane);if(!it)return nullptr;
        IORegistryEntry *found=nullptr;
        while(OSObject *child=it->getNextObject()) {
            IORegistryEntry *entry=OSDynamicCast(IORegistryEntry,child);
            if(!entry)continue;
            found=entry->metaCast("IOHIDEventDriver")?entry:eventDriver(entry,depth-1);
            if(found)break;
        }
        it->release();return found;
    }
    static void tick(OSObject *owner,IOTimerEventSource *source) {
        NativeHidInput *self=OSDynamicCast(NativeHidInput,owner);if(!self)return;
        native_hid_live_checkpoint(self);
        if(!self->touchOnly && !self->consumerOnly) {
            if(IORegistryEntry *driver=eventDriver(self,4)) kprobe::install(driver);
        }
        // fb219/fb220: BKS registers its HIDServiceSupport notification tens of seconds after our
        // services publish. Re-publish an unopened driver periodically so a listener that is up now
        // receives a fresh match; stop as soon as a client holds it open.
        // fb221: a single static counter made only the device whose timer happened to land on the
        // threshold publish its driver, so the keyboard service was never registered and BKS never
        // saw it. Count per instance: every device publishes its own driver, starting at ~20 s.
        ++self->kickTicks;
        // fb255: the 20 s / 60 s tree samples can miss a client that opens and closes between them
        // (fb249 showed an IOHIDEventServiceUserClient child, fb252/254 none). Watch the open state
        // every second and, on a change, print the service's children with their creator process --
        // IOUserClient stamps kIOUserClientCreatorKey with "<pid> <name>" at open time.
        if(IORegistryEntry *driver=eventDriver(self,4)) {
            IOService *service=OSDynamicCast(IOService,driver);
            if(service) {
                const unsigned openNow=service->isOpen()?1u:0u;
                if(openNow!=self->lastOpen) {
                    self->lastOpen=openNow;
                    OSString *creator=OSDynamicCast(OSString,service->getProperty("IOUserClientCreator"));
                    kprintf("YUNLUO-INPUT opedge %s open=%u creator=%s\n",
                            self->touchOnly?"touch":(self->consumerOnly?"cons":"kb"),openNow,
                            creator?creator->getCStringNoCopy():"-");
                    OSIterator *kids=service->getChildIterator(gIOServicePlane);
                    if(kids) {
                        while(OSObject *c=kids->getNextObject()) {
                            IORegistryEntry *e=OSDynamicCast(IORegistryEntry,c);
                            if(!e)continue;
                            OSString *cr=OSDynamicCast(OSString,e->getProperty("IOUserClientCreator"));
                            if(e->metaCast("IOUserClient"))
                                kprintf("YUNLUO-INPUT opclient cls=%s creator=%s\n",
                                        e->getMetaClass()->getClassName(),
                                        cr?cr->getCStringNoCopy():"-");
                        }
                        kids->release();
                    }
                }
            }
        }
        if(self->kickTicks>=20 && (self->kickTicks%10)==0) {
            if(IORegistryEntry *driver=eventDriver(self,4)) {
                IOService *service=OSDynamicCast(IOService,driver);
                if(service && !service->isOpen()) {
                    service->registerService();
                    kprintf("YUNLUO-INPUT kick %s drv=%llx\n", self->touchOnly?"touch":(self->consumerOnly?"cons":"kb"),
                            (unsigned long long)(uintptr_t)service);
                }
            }
        }
        // Publish this device's own class on its event service. The digitizer is (0x0D,4) with
        // Built-In, the consumer control device (volume/power) is (0x0C,1), the keyboard (0x01,6).
        if(self->touchOnly) native_hid_publish_service_class(self,0x0d,4,1);
        else if(self->consumerOnly) native_hid_publish_service_class(self,0x0c,1,0);
        else native_hid_publish_service_class(self,0x01,0x06,0);
        if(self->touchOnly){source->setTimeoutMS(1000);return;}
        if(!self->down && !hasEventDriver(self,4)) {
            native_hid_input_observed(3,0);
            source->setTimeoutMS(1000);
            return;
        }
        native_hid_input_observed(3,1);
        if(!self->down && !native_hid_input_requested()) {source->setTimeoutMS(1000);return;}
        keys::setBit(native_hid_auto_bit, self->down?0:1);
        native_hid_input_observed(self->down?5:4,0);
        // Always attempt release, even if down returned an error: input must
        // never be left pressed by an ambiguous report completion.
        if(!self->down) {self->down=true;source->setTimeoutMS(100);}
        else {self->down=false;source->setTimeoutMS(1000);}
    }
public:
    IOWorkLoop *getWorkLoop() const override {return inputLoop;}
    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    // IOHIDEventDriver's only personality that accepts a Consumer Control pair is
    // "Peppy Touch Display" (DeviceUsagePairs 0x0c/0x01) and it also requires Transport=FIFO
    // (kernel.macho __PRELINK_INFO). A Virtual transport matches no personality at all, so the
    // consumer device never gets an event driver or an IOHIDEventService. The digitizer needs the
    // same transport spelling for the same reason: SPI is what the board really uses, but no
    // personality matches it.
    OSString *newTransportString() const override {return OSString::withCString(consumerOnly||touchOnly?"FIFO":"Virtual");}
    OSString *newProductString() const override {return OSString::withCString(touchOnly?"Offline touch digitizer":"Offline HID input probe");}
    IOReturn newReportDescriptor(IOMemoryDescriptor **out) const override {
        if(!out)return kIOReturnBadArgument;
        unsigned char combined[sizeof(descriptor)+sizeof(touchDescriptor)];
        size_t size;
        if(consumerOnly){size=sizeof(consumerDescriptor);memcpy(combined,consumerDescriptor,size);}
        else if(touchOnly){size=sizeof(touchDescriptor);memcpy(combined,touchDescriptor,size);}
        else{
            size=sizeof(descriptor);memcpy(combined,descriptor,size);
            // Default: one device, keyboard collection first, digitizer second (the arrangement whose event driver
            // and user client attach on hardware); the separate digitizer device is the opt-in sheng-touch-split.
            if(touchEnabled){memcpy(combined+size,touchDescriptor,sizeof(touchDescriptor));size+=sizeof(touchDescriptor);}
        }
        *out=IOBufferMemoryDescriptor::withBytes(combined,size,kIODirectionNone);
        return *out?kIOReturnSuccess:kIOReturnNoMemory;
    }
    bool start(IOService *provider) override {
        if(!keys::prepare())return false;
        inputLoop=IOWorkLoop::workLoop();if(!inputLoop)return false;
        // Request the normal generic HID event personality for this owned
        // keyboard. This is a matching property, not a client entitlement.
        if(!setProperty("HIDDefaultBehavior",true))return false;
        unsigned touch=0;
        touchEnabled=PE_parse_boot_argn("sheng-touch-hid",&touch,sizeof(touch)) && touch==1;
        // Split mode: the separate digitizer device carries the touch descriptor. Appending it here
        // as well gave this keyboard service the 0xff00/0x02 pair too, so backboardd's touchscreen
        // match fired for two devices and the keyboard's own Consumer events were routed as a
        // digitizer service (fb208: two NOTIFY firings, two IOHIDLibUserClient opens, no key effect).
        unsigned split=0;
        PE_parse_boot_argn("sheng-touch-split",&split,sizeof(split));
        if(touchEnabled && split==1) touchEnabled=false;
        if(consumerOnly)setProperty("HIDDefaultBehavior",true);
        if(touchOnly||touchEnabled) {
            unsigned rotation=0;
            PE_parse_boot_argn("yunluo-touch-rotate",&rotation,sizeof(rotation));
            if(rotation!=0 && rotation!=180)return false;
            touchRotate180=rotation==180;
            setProperty("NativeTouchRotation",rotation,32);
            kprintf("YUNLUO-DISPLAY touch rotation=%u raw-to-logical before normalization\n",rotation);
            setProperty("Built-In",true);
            setProperty("DisplayIntegrated",true);
            if(!setProperty("DigitizerCollectionDispatch",true))return false;
            setProperty("QueueSize",0x10000u,32);
        }
        // newTransportString() alone was not enough: fb245 still showed tr=SPI on the touch device,
        // so the property is stamped here as well, before registerService makes it matchable.
        if(touchOnly)setProperty("Transport",OSString::withCString("FIFO"));
        if(!IOHIDDevice::start(provider))return false;
        // Pre-seed the interface (d=1) as soon as it exists and BEFORE any event driver can start on
        // it. BKS's HID notification is a property match, evaluated when the *event service* is
        // registered; a property written afterwards (our 1 s tick) never re-fires it.
        {
            OSIterator *it=getChildIterator(gIOServicePlane);
            if(it) {
                while(OSObject *child=it->getNextObject()) {
                    IORegistryEntry *entry=OSDynamicCast(IORegistryEntry,child);
                    if(!entry || !entry->metaCast("IOHIDInterface"))continue;
                    entry->setProperty("HIDServiceSupport",kOSBooleanTrue);
                    if(touchOnly) {
                        entry->setProperty("PrimaryUsagePage",0x0du,32);
                        entry->setProperty("PrimaryUsage",0x04u,32);
                        entry->setProperty("Built-In",kOSBooleanTrue);
                        entry->setProperty("DisplayIntegrated",kOSBooleanTrue);
                        entry->setProperty("DigitizerCollectionDispatch",kOSBooleanTrue);
                        entry->setProperty("QueueSize",0x10000u,32);
                    } else if(consumerOnly) {
                        entry->setProperty("PrimaryUsagePage",0x0cu,32);
                        entry->setProperty("PrimaryUsage",0x01u,32);
                    } else {
                        entry->setProperty("PrimaryUsagePage",0x01u,32);
                        entry->setProperty("PrimaryUsage",0x06u,32);
                    }
                    kprintf("YUNLUO-INPUT preseed iface=%llx cls=%s hss=1\n",
                            (unsigned long long)(uintptr_t)entry,entry->getMetaClass()->getClassName());
                }
                it->release();
            }
        }
        timer=IOTimerEventSource::timerEventSource(this,tick);
        if(!timer || inputLoop->addEventSource(timer)!=kIOReturnSuccess) {
            OSSafeReleaseNULL(timer);IOHIDDevice::stop(provider);return false;
        }
        if(touchOnly||touchEnabled) {
            if(&native_touch_set_sink)native_touch_set_sink(&NativeHidInput::touchNotify,this);
        }
        // The consumer key queue is served by its own source: keys must not wait on the touch rate.
        // The digitizer must never become the key sink: in the split layout it is created first, so
        // an unconditional attach would bind every key report to a device that has no event driver.
        if(!touchOnly)keys::attach(this, consumerOnly);
        keyTimer=IOTimerEventSource::timerEventSource(this,keys::deliver);
        if(keyTimer && inputLoop->addEventSource(keyTimer)==kIOReturnSuccess)keyTimer->setTimeoutMS(10);
        else OSSafeReleaseNULL(keyTimer);
        setProperty("OfflineInputProbe",true);
        native_hid_input_observed(2,0);
        timer->setTimeoutMS(1000);return true;
    }
    void stop(IOService *provider) override {
        if(keyTimer){keyTimer->cancelTimeout();inputLoop->removeEventSource(keyTimer);OSSafeReleaseNULL(keyTimer);}
        keys::detach(this);
        if(touchTimer){touchTimer->cancelTimeout();inputLoop->removeEventSource(touchTimer);OSSafeReleaseNULL(touchTimer);}
        if((touchOnly||touchEnabled) && &native_touch_set_sink)native_touch_set_sink(nullptr,nullptr);
        if(timer){timer->cancelTimeout();inputLoop->removeEventSource(timer);OSSafeReleaseNULL(timer);}
        IOHIDDevice::stop(provider);
    }
    void free() override {OSSafeReleaseNULL(inputLoop);IOHIDDevice::free();}
};
OSDefineMetaClassAndStructors(NativeHidInput,IOHIDDevice)
bool NativeHidInput::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    const bool opened=IOHIDDevice::handleOpen(forClient,options,arg);
    // Sticky signal for the key-device trigger: a client (the userspace HID stack) opened the
    // digitizer. Polling isOpen() would miss its short open/close pair.
    if(opened && touchOnly) {
        native_touch_opened=1;
        const char *name=forClient?forClient->getName():nullptr;
        kprintf("YUNLUO-INPUT touch open client=%llx %s\n",
                (unsigned long long)(uintptr_t)forClient,name?name:"?");
    }
    // IOHIDEventDriver opens this device from its own start(), before it registers the event
    // service. Its HIDServiceSupport is merged by IOHIDProviderPropertyMerger afterwards, so BKS'
    // publish-time match never sees it (fb218: driver exists with hss=T but open=0). Publish the
    // classification on the client now, before its registerService(), so the property is present
    // at publish time and no notification churn is caused on an already-open service.
    if(opened && forClient && forClient->metaCast("IOHIDEventDriver")) {
        unsigned long long page=0x01,usage=0x06;
        if(touchOnly){page=0x0d;usage=0x04;}
        else if(consumerOnly){page=0x0c;usage=0x01;}
        forClient->setProperty("PrimaryUsagePage",page,32);
        forClient->setProperty("PrimaryUsage",usage,32);
        if(touchOnly) {
            forClient->setProperty("Built-In",kOSBooleanTrue);
            forClient->setProperty("DisplayIntegrated",kOSBooleanTrue);
            forClient->setProperty("DigitizerCollectionDispatch",kOSBooleanTrue);
            forClient->setProperty("QueueSize",0x10000u,32);
        }
        forClient->setProperty("HIDServiceSupport",kOSBooleanTrue);
        // The IOHIDEventServiceUserClient personality merges this with HIDServiceSupport. Set it
        // explicitly as well so the service is openable even if that merger runs late.
        OSString *clientClass=OSString::withCString("IOHIDEventServiceUserClient");
        if(clientClass){forClient->setProperty("IOUserClientClass",clientClass);clientClass->release();}
        kprintf("YUNLUO-INPUT earlyclass client=%llx page=%02llx usage=%llx\n",
                (unsigned long long)(uintptr_t)forClient,page,usage);
    }
    return opened;
}
// Consumer key queue: the board probe pushes (bit, down) and each entry becomes one report 2.
extern "C" void native_hid_input_key(unsigned short usage,int down) {
    const unsigned char bit=keys::bitFor(usage);
    if(bit==0xff)return;
    keys::setBit(bit,down);
}
// Direct bit form, so a board can name a consumer bit without a usage value (0 Menu, 1 Power,
// 2 Volume Increment, 3 Volume Decrement).
extern "C" void native_hid_input_bit(unsigned char bit,int down) {
    if(bit>3)return;
    keys::setBit(bit,down);
}
extern "C" bool native_hid_input_prepare(void) {return keys::prepare();}

extern "C" IOService *native_hid_input_allocate(void) {return new NativeHidInput;}
extern "C" IOService *native_hid_touch_allocate(void) {NativeHidInput *device=new NativeHidInput;if(device)device->setTouchOnly();return device;}
extern "C" IOService *native_hid_consumer_allocate(void) {NativeHidInput *device=new NativeHidInput;if(device)device->setConsumerOnly();return device;}
