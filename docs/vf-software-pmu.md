# i915 VF software PMU

The i915 VF driver exposes a software PMU by default using the same context
runtime counters as its existing DRM fdinfo interface. It requires only a
patched **guest** driver. The host can keep the original i915-sriov-dkms driver
and firmware; no PF changes, new GuC actions, or PF/VF relay protocol are used.
The i915 option applies only to i915 VFs.

This is a software view of guest context runtime, not a hardware PMU. It does
not restore access to the firmware statistics that GuC denies to a VF.

## Use in the guest

Use the i915 driver in the guest. If testing xe previously, remove
`module_blacklist=i915` and `xe.force_probe=46d4` from the kernel command line.
Keep xe blacklisted to select i915:

```text
module_blacklist=xe
```

No explicit `i915.enable_vf_pmu=1` option is required. Remove any existing
`i915.enable_vf_pmu=0` or `options i915 enable_vf_pmu=0` override. Rebuild the
initramfs if it contains i915, update the bootloader configuration as
appropriate for the distribution, and reboot the guest. The guest kernel must
have CONFIG_PERF_EVENTS enabled.

The kernel log should contain:

```text
Software VF PMU enabled: class-average context runtime, not hardware engine counters
```

Find the PMU name with:

```sh
ls -d /sys/bus/event_source/devices/i915*
```

A VF at `0000:09:00.0` uses `i915_0000_09_00.0`. VF names follow the guest PCI
address, with colons replaced by underscores. The conventional integrated-GPU
address `0000:00:02.0` uses the legacy name `i915`, matching intel_gpu_top's
device selection. Inspect the advertised semantics with:

```sh
cat /sys/bus/event_source/devices/i915_0000_09_00.0/caps/software
cat /sys/bus/event_source/devices/i915_0000_09_00.0/caps/busy_aggregation
cat /sys/bus/event_source/devices/i915_0000_09_00.0/caps/sampling_period_ms
```

These files report `1`, `class-average`, and `20`, respectively. Substitute the
actual PMU name for a different PCI address.

## Read the counters

The software PMU retains the standard i915 event encodings and nanosecond busy
time units. Each engine class exposes only its instance-zero `*-busy` event,
such as `rcs0-busy` or `vcs0-busy`. This is a virtual representative of the
whole class. It does not identify the physical engine that ran a load-balanced
context. The counter is the sum of measured context runtimes divided by the
number of user-visible engines in that class.

For a class with one engine, this is its measured context runtime. For a class
with two engines, one fully occupied engine corresponds to an average of 50%.
The counters accumulate independently of process and context lifetimes, so
closing a DRM file does not discard already accounted runtime.

The `interrupts` event reports interrupts handled by this guest's i915 driver.
There are no wait, semaphore, frequency, RC6, or other physical GPU events.
Their absence is intentional; zero must not be interpreted as a measurement
for an unsupported event.

For a VF at `0000:09:00.0`, sample render and video class counters with:

```sh
sudo perf stat -a -I 1000 \
    -e '{i915_0000_09_00.0/rcs0-busy/,i915_0000_09_00.0/vcs0-busy/}'
```

Use only classes listed in the PMU's `events` directory. Average utilization
is `100 * delta(busy_ns) / delta(wall_time_ns)`.

`intel_gpu_top` discovers engines through the standard `*-busy` events. Its
mandatory interrupt event is also provided; the other hardware events are
optional. List devices with `intel_gpu_top -L`, then select the guest's i915
DRM device, for example:

```sh
sudo intel_gpu_top -d drm:/dev/dri/card0
```

Some tool versions may display zero or empty fields for missing optional
events. Those fields are unavailable metrics. Versions that assume every
hardware event exists may still need a userspace update.

## Accuracy and compatibility

The driver samples pinned context state approximately every 20 ms while the
guest GPU is awake. Runtime is saved by the GPU at context switch boundaries;
a long-running context can therefore cause delayed or bursty updates. Prefer
sampling intervals of at least 500 ms. Context runtime also includes time a
resident context spends waiting on the GPU; it is not a measure of shader
efficiency or the VF's entitlement to physical GPU scheduling time.

Fdinfo reads, periodic samples, and final context unpin share the same runtime
delta accounting. Polling fdinfo alongside perf does not double-count runtime.
Only this guest's context state is read, without reading global GPU registers
or collecting another VF's counters.

The change preserves DRM ioctls, normal command submission, and the existing
PF/VF protocol. The guest driver must carry this patch; it is not a capability
provided by an unmodified upstream VF driver.
Future upstream source updates may require adapting the patch.

On the target machine, verify idle and active workloads, short-lived contexts,
parallel clients, class capacity normalization, VF reset, suspend/resume, and
driver unload/reload. Compilation and software simulations do not establish
hardware accuracy or runtime stability.

To disable the software PMU and restore the original VF behavior, set
`i915.enable_vf_pmu=0` in the guest's kernel command line or
`options i915 enable_vf_pmu=0` in its modprobe configuration. Update the
bootloader configuration and initramfs as appropriate, then reboot the guest.

## Xe VF software PMU

The Xe VF software PMU is disabled by default. To enable it, set
`xe.enable_vf_pmu=1` on the guest kernel command line or
`options xe enable_vf_pmu=1` in the guest modprobe configuration. The guest
kernel must have `CONFIG_PERF_EVENTS` enabled, and Xe must have the GT reference
clock. The existing host driver and GuC firmware can provide the normal VF
initialization and clock information.

For a guest bound to Xe at PCI address `0000:09:00.0`, read the render class
with the standard Xe event format:

```sh
pmu=xe_0000_09_00.0
cat /sys/bus/event_source/devices/$pmu/caps/software
cat /sys/bus/event_source/devices/$pmu/caps/busy_aggregation
sudo perf stat -a -I 1000 \
    -e "{$pmu/engine-active-ticks,gt=0,engine_class=0,engine_instance=0/,$pmu/engine-total-ticks,gt=0,engine_class=0,engine_instance=0/}"
```

Calculate utilization as `100 * delta(active_ticks) / delta(total_ticks)`.
Active ticks sum saved guest context runtime and divide by the visible engine
count in that class on the selected GT. All instances in a class return the
same average. Total ticks measure guest monotonic time in GT reference clock
ticks. The software PMU exposes these two engine events for the local VF
(`function=0`). Xe-aware tools that open both events for every engine can use
the counters directly.

Xe samples saved context timestamps about every 20 ms while a perf event is
open, and collects the last saved timestamp on queue teardown. Samples may
arrive in bursts when a context stays resident for a long time. Compare idle
and active intervals over at least 500 ms. Use a tool with a Xe backend for
this PMU.
