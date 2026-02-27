/*
 * Copyright © 2006 Keith Packard
 * Copyright © 2007-2008 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __DRM_CRTC_H__
#define __DRM_CRTC_H__

#include <linux/i2c.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/hdmi.h>
#include <linux/media-bus-format.h>
#include <uapi/drm/drm_mode.h>
#include <uapi/drm/drm_fourcc.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_rect.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modes.h>
#include <drm/drm_connector.h>
#include <drm/drm_property.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
#include <drm/drm_plane.h>
#include <drm/drm_blend.h>
#include <drm/drm_color_mgmt.h>
#include <drm/drm_debugfs_crc.h>
#include <drm/drm_mode_config.h>

struct drm_device;
struct drm_mode_set;
struct drm_file;
struct drm_clip_rect;
struct drm_printer;
struct device_node;
struct dma_fence;
struct edid;

static inline int64_t U642I64(uint64_t val)
{
	return (int64_t)*((int64_t *)&val);
}
static inline uint64_t I642U64(int64_t val)
{
	return (uint64_t)*((uint64_t *)&val);
}

struct drm_crtc;
struct drm_pending_vblank_event;
struct drm_plane;
struct drm_bridge;
struct drm_atomic_state;

struct drm_crtc_helper_funcs;
struct drm_plane_helper_funcs;

/**
 * struct drm_crtc_state - mutable CRTC state
 *
 * Note that the distinction between @enable and @active is rather subtile:
 * Flipping @active while @enable is set without changing anything else may
 * never return in a failure from the &drm_mode_config_funcs.atomic_check
 * callback. Userspace assumes that a DPMS On will always succeed. In other
 * words: @enable controls resource assignment, @active controls the actual
 * hardware state.
 *
 * The three booleans active_changed, connectors_changed and mode_changed are
 * intended to indicate whether a full modeset is needed, rather than strictly
 * describing what has changed in a commit. See also:
 * drm_atomic_crtc_needs_modeset()
 *
 * WARNING: Transitional helpers (like drm_helper_crtc_mode_set() or
 * drm_helper_crtc_mode_set_base()) do not maintain many of the derived control
 * state like @plane_mask so drivers not converted over to atomic helpers should
 * not rely on these being accurate!
 */
struct drm_crtc_state {
	/** @crtc: backpointer to the CRTC */
	struct drm_crtc *crtc;

	/**
	 * @enable: Whether the CRTC should be enabled, gates all other state.
	 * This controls reservations of shared resources. Actual hardware state
	 * is controlled by @active.
	 */
	bool enable;

	/**
	 * @active: Whether the CRTC is actively displaying (used for DPMS).
	 * Implies that @enable is set. The driver must not release any shared
	 * resources if @active is set to false but @enable still true, because
	 * userspace expects that a DPMS ON always succeeds.
	 *
	 * Hence drivers must not consult @active in their various
	 * &drm_mode_config_funcs.atomic_check callback to reject an atomic
	 * commit. They can consult it to aid in the computation of derived
	 * hardware state, since even in the DPMS OFF state the display hardware
	 * should be as much powered down as when the CRTC is completely
	 * disabled through setting @enable to false.
	 */
	bool active;

	/**
	 * @planes_changed: Planes on this crtc are updated. Used by the atomic
	 * helpers and drivers to steer the atomic commit control flow.
	 */
	bool planes_changed : 1;

	/**
	 * @mode_changed: @mode or @enable has been changed. Used by the atomic
	 * helpers and drivers to steer the atomic commit control flow. See also
	 * drm_atomic_crtc_needs_modeset().
	 *
	 * Drivers are supposed to set this for any CRTC state changes that
	 * require a full modeset. They can also reset it to false if e.g. a
	 * @mode change can be done without a full modeset by only changing
	 * scaler settings.
	 */
	bool mode_changed : 1;

	/**
	 * @active_changed: @active has been toggled. Used by the atomic
	 * helpers and drivers to steer the atomic commit control flow. See also
	 * drm_atomic_crtc_needs_modeset().
	 */
	bool active_changed : 1;

	/**
	 * @connectors_changed: Connectors to this crtc have been updated,
	 * either in their state or routing. Used by the atomic
	 * helpers and drivers to steer the atomic commit control flow. See also
	 * drm_atomic_crtc_needs_modeset().
	 *
	 * Drivers are supposed to set this as-needed from their own atomic
	 * check code, e.g. from &drm_encoder_helper_funcs.atomic_check
	 */
	bool connectors_changed : 1;
	/**
	 * @zpos_changed: zpos values of planes on this crtc have been updated.
	 * Used by the atomic helpers and drivers to steer the atomic commit
	 * control flow.
	 */
	bool zpos_changed : 1;
	/**
	 * @color_mgmt_changed: Color management properties have changed
	 * (@gamma_lut, @degamma_lut or @ctm). Used by the atomic helpers and
	 * drivers to steer the atomic commit control flow.
	 */
	bool color_mgmt_changed : 1;

	/**
	 * @no_vblank:
	 *
	 * Reflects the ability of a CRTC to send VBLANK events. This state
	 * usually depends on the pipeline configuration, and the main usuage
	 * is CRTCs feeding a writeback connector operating in oneshot mode.
	 * In this case the VBLANK event is only generated when a job is queued
	 * to the writeback connector, and we want the core to fake VBLANK
	 * events when this part of the pipeline hasn't changed but others had
	 * or when the CRTC and connectors are being disabled.
	 *
	 * __drm_atomic_helper_crtc_duplicate_state() will not reset the value
	 * from the current state, the CRTC driver is then responsible for
	 * updating this field when needed.
	 *
	 * Note that the combination of &drm_crtc_state.event == NULL and
	 * &drm_crtc_state.no_blank == true is valid and usually used when the
	 * writeback connector attached to the CRTC has a new job queued. In
	 * this case the driver will send the VBLANK event on its own when the
	 * writeback job is complete.
	 */
	bool no_vblank : 1;

	/**
	 * @plane_mask: Bitmask of drm_plane_mask(plane) of planes attached to
	 * this CRTC.
	 */
	u32 plane_mask;

	/**
	 * @connector_mask: Bitmask of drm_connector_mask(connector) of
	 * connectors attached to this CRTC.
	 */
	u32 connector_mask;

	/**
	 * @encoder_mask: Bitmask of drm_encoder_mask(encoder) of encoders
	 * attached to this CRTC.
	 */
	u32 encoder_mask;

	/**
	 * @adjusted_mode:
	 *
	 * Internal display timings which can be used by the driver to handle
	 * differences between the mode requested by userspace in @mode and what
	 * is actually programmed into the hardware.
	 *
	 * For drivers using &drm_bridge, this stores hardware display timings
	 * used between the CRTC and the first bridge. For other drivers, the
	 * meaning of the adjusted_mode field is purely driver implementation
	 * defined information, and will usually be used to store the hardware
	 * display timings used between the CRTC and encoder blocks.
	 */
	struct drm_display_mode adjusted_mode;

	/**
	 * @mode:
	 *
	 * Display timings requested by userspace. The driver should try to
	 * match the refresh rate as close as possible (but note that it's
	 * undefined what exactly is close enough, e.g. some of the HDMI modes
	 * only differ in less than 1% of the refresh rate). The active width
	 * and height as observed by userspace for positioning planes must match
	 * exactly.
	 *
	 * For external connectors where the sink isn't fixed (like with a
	 * built-in panel), this mode here should match the physical mode on the
	 * wire to the last details (i.e. including sync polarities and
	 * everything).
	 */
	struct drm_display_mode mode;

	/**
	 * @mode_blob: &drm_property_blob for @mode, for exposing the mode to
	 * atomic userspace.
	 */
	struct drm_property_blob *mode_blob;

	/**
	 * @degamma_lut:
	 *
	 * Lookup table for converting framebuffer pixel data before apply the
	 * color conversion matrix @ctm. See drm_crtc_enable_color_mgmt(). The
	 * blob (if not NULL) is an array of &struct drm_color_lut.
	 */
	struct drm_property_blob *degamma_lut;

	/**
	 * @ctm:
	 *
	 * Color transformation matrix. See drm_crtc_enable_color_mgmt(). The
	 * blob (if not NULL) is a &struct drm_color_ctm.
	 */
	struct drm_property_blob *ctm;

	/**
	 * @gamma_lut:
	 *
	 * Lookup table for converting pixel data after the color conversion
	 * matrix @ctm.  See drm_crtc_enable_color_mgmt(). The blob (if not
	 * NULL) is an array of &struct drm_color_lut.
	 */
	struct drm_property_blob *gamma_lut;

	/**
	 * @cubic_lut:
	 *
	 * Cubic Lookup table for converting pixel data. See
	 * drm_crtc_enable_color_mgmt(). The blob (if not NULL) is a 3D array
	 * of &struct drm_color_lut.
	 */
	struct drm_property_blob *cubic_lut;

	/**
	 * @target_vblank:
	 *
	 * Target vertical blank period when a page flip
	 * should take effect.
	 */
	u32 target_vblank;

	/**
	 * @pageflip_flags:
	 *
	 * DRM_MODE_PAGE_FLIP_* flags, as passed to the page flip ioctl.
	 * Zero in any other case.
	 */
	u32 pageflip_flags;

	/**
	 * @event:
	 *
	 * Optional pointer to a DRM event to signal upon completion of the
	 * state update. The driver must send out the event when the atomic
	 * commit operation completes. There are two cases:
	 *
	 *  - The event is for a CRTC which is being disabled through this
	 *    atomic commit. In that case the event can be send out any time
	 *    after the hardware has stopped scanning out the current
	 *    framebuffers. It should contain the timestamp and counter for the
	 *    last vblank before the display pipeline was shut off. The simplest
	 *    way to achieve that is calling drm_crtc_send_vblank_event()
	 *    somewhen after drm_crtc_vblank_off() has been called.
	 *
	 *  - For a CRTC which is enabled at the end of the commit (even when it
	 *    undergoes an full modeset) the vblank timestamp and counter must
	 *    be for the vblank right before the first frame that scans out the
	 *    new set of buffers. Again the event can only be sent out after the
	 *    hardware has stopped scanning out the old buffers.
	 *
	 *  - Events for disabled CRTCs are not allowed, and drivers can ignore
	 *    that case.
	 *
	 * This can be handled by the drm_crtc_send_vblank_event() function,
	 * which the driver should call on the provided event upon completion of
	 * the atomic commit. Note that if the driver supports vblank signalling
	 * and timestamping the vblank counters and timestamps must agree with
	 * the ones returned from page flip events. With the current vblank
	 * helper infrastructure this can be achieved by holding a vblank
	 * reference while the page flip is pending, acquired through
	 * drm_crtc_vblank_get() and released with drm_crtc_vblank_put().
	 * Drivers are free to implement their own vblank counter and timestamp
	 * tracking though, e.g. if they have accurate timestamp registers in
	 * hardware.
	 *
	 * For hardware which supports some means to synchronize vblank
	 * interrupt delivery with committing display state there's also
	 * drm_crtc_arm_vblank_event(). See the documentation of that function
	 * for a detailed discussion of the constraints it needs to be used
	 * safely.
	 *
	 * If the device can't notify of flip completion in a race-free way
	 * at all, then the event should be armed just after the page flip is
	 * committed. In the worst case the driver will send the event to
	 * userspace one frame too late. This doesn't allow for a real atomic
	 * update, but it should avoid tearing.
	 */
	struct drm_pending_vblank_event *event;

	/**
	 * @commit:
	 *
	 * This tracks how the commit for this update proceeds through the
	 * various phases. This is never cleared, except when we destroy the
	 * state, so that subsequent commits can synchronize with previous ones.
	 */
	struct drm_crtc_commit *commit;

	/** @state: backpointer to global drm_atomic_state */
	struct drm_atomic_state *state;
};

/**
 * struct drm_crtc_funcs - control CRTCs for a given device
 *
 * The drm_crtc_funcs structure is the central CRTC management structure
 * in the DRM.  Each CRTC controls one or more connectors (note that the name
 * CRTC is simply historical, a CRTC may control LVDS, VGA, DVI, TV out, etc.
 * connectors, not just CRTs).
 *
 * Each driver is responsible for filling out this structure at startup time,
 * in addition to providing other modesetting features, like i2c and DDC
 * bus accessors.
 */
struct drm_crtc_funcs {
	/**
	 * @reset:
	 *
	 * Reset CRTC hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 *
	 * Atomic drivers can use drm_atomic_helper_crtc_reset() to reset
	 * atomic state using this hook.
	 */
	void (*reset)(struct drm_crtc *crtc);

	/**
	 * @cursor_set:
	 *
	 * Update the cursor image. The cursor position is relative to the CRTC
	 * and can be partially or fully outside of the visible area.
	 *
	 * Note that contrary to all other KMS functions the legacy cursor entry
	 * points don't take a framebuffer object, but instead take directly a
	 * raw buffer object id from the driver's buffer manager (which is
	 * either GEM or TTM for current drivers).
	 *
	 * This entry point is deprecated, drivers should instead implement
	 * universal plane support and register a proper cursor plane using
	 * drm_crtc_init_with_planes().
	 *
	 * This callback is optional
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*cursor_set)(struct drm_crtc *crtc, struct drm_file *file_priv,
			  uint32_t handle, uint32_t width, uint32_t height);

	/**
	 * @cursor_set2:
	 *
	 * Update the cursor image, including hotspot information. The hotspot
	 * must not affect the cursor position in CRTC coordinates, but is only
	 * meant as a hint for virtualized display hardware to coordinate the
	 * guests and hosts cursor position. The cursor hotspot is relative to
	 * the cursor image. Otherwise this works exactly like @cursor_set.
	 *
	 * This entry point is deprecated, drivers should instead implement
	 * universal plane support and register a proper cursor plane using
	 * drm_crtc_init_with_planes().
	 *
	 * This callback is optional.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*cursor_set2)(struct drm_crtc *crtc, struct drm_file *file_priv,
			   uint32_t handle, uint32_t width, uint32_t height,
			   int32_t hot_x, int32_t hot_y);

	/**
	 * @cursor_move:
	 *
	 * Update the cursor position. The cursor does not need to be visible
	 * when this hook is called.
	 *
	 * This entry point is deprecated, drivers should instead implement
	 * universal plane support and register a proper cursor plane using
	 * drm_crtc_init_with_planes().
	 *
	 * This callback is optional.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*cursor_move)(struct drm_crtc *crtc, int x, int y);

	/**
	 * @gamma_set:
	 *
	 * Set gamma on the CRTC.
	 *
	 * This callback is optional.
	 *
	 * Atomic drivers who want to support gamma tables should implement the
	 * atomic color management support, enabled by calling
	 * drm_crtc_enable_color_mgmt(), which then supports the legacy gamma
	 * interface through the drm_atomic_helper_legacy_gamma_set()
	 * compatibility implementation.
	 */
	int (*gamma_set)(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
			 uint32_t size,
			 struct drm_modeset_acquire_ctx *ctx);

	/**
	 * @destroy:
	 *
	 * Clean up plane resources. This is only called at driver unload time
	 * through drm_mode_config_cleanup() since a CRTC cannot be hotplugged
	 * in DRM.
	 */
	void (*destroy)(struct drm_crtc *crtc);

	/**
	 * @set_config:
	 *
	 * This is the main legacy entry point to change the modeset state on a
	 * CRTC. All the details of the desired configuration are passed in a
	 * &struct drm_mode_set - see there for details.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_set_config() to implement this hook.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_config)(struct drm_mode_set *set,
			  struct drm_modeset_acquire_ctx *ctx);

	/**
	 * @page_flip:
	 *
	 * Legacy entry point to schedule a flip to the given framebuffer.
	 *
	 * Page flipping is a synchronization mechanism that replaces the frame
	 * buffer being scanned out by the CRTC with a new frame buffer during
	 * vertical blanking, avoiding tearing (except when requested otherwise
	 * through the DRM_MODE_PAGE_FLIP_ASYNC flag). When an application
	 * requests a page flip the DRM core verifies that the new frame buffer
	 * is large enough to be scanned out by the CRTC in the currently
	 * configured mode and then calls this hook with a pointer to the new
	 * frame buffer.
	 *
	 * The driver must wait for any pending rendering to the new framebuffer
	 * to complete before executing the flip. It should also wait for any
	 * pending rendering from other drivers if the underlying buffer is a
	 * shared dma-buf.
	 *
	 * An application can request to be notified when the page flip has
	 * completed. The drm core will supply a &struct drm_event in the event
	 * parameter in this case. This can be handled by the
	 * drm_crtc_send_vblank_event() function, which the driver should call on
	 * the provided event upon completion of the flip. Note that if
	 * the driver supports vblank signalling and timestamping the vblank
	 * counters and timestamps must agree with the ones returned from page
	 * flip events. With the current vblank helper infrastructure this can
	 * be achieved by holding a vblank reference while the page flip is
	 * pending, acquired through drm_crtc_vblank_get() and released with
	 * drm_crtc_vblank_put(). Drivers are free to implement their own vblank
	 * counter and timestamp tracking though, e.g. if they have accurate
	 * timestamp registers in hardware.
	 *
	 * This callback is optional.
	 *
	 * NOTE:
	 *
	 * Very early versions of the KMS ABI mandated that the driver must
	 * block (but not reject) any rendering to the old framebuffer until the
	 * flip operation has completed and the old framebuffer is no longer
	 * visible. This requirement has been lifted, and userspace is instead
	 * expected to request delivery of an event and wait with recycling old
	 * buffers until such has been received.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure. Note that if a
	 * page flip operation is already pending the callback should return
	 * -EBUSY. Pageflips on a disabled CRTC (either by setting a NULL mode
	 * or just runtime disabled through DPMS respectively the new atomic
	 * "ACTIVE" state) should result in an -EINVAL error code. Note that
	 * drm_atomic_helper_page_flip() checks this already for atomic drivers.
	 */
	int (*page_flip)(struct drm_crtc *crtc,
			 struct drm_framebuffer *fb,
			 struct drm_pending_vblank_event *event,
			 uint32_t flags,
			 struct drm_modeset_acquire_ctx *ctx);

	/**
	 * @page_flip_target:
	 *
	 * Same as @page_flip but with an additional parameter specifying the
	 * absolute target vertical blank period (as reported by
	 * drm_crtc_vblank_count()) when the flip should take effect.
	 *
	 * Note that the core code calls drm_crtc_vblank_get before this entry
	 * point, and will call drm_crtc_vblank_put if this entry point returns
	 * any non-0 error code. It's the driver's responsibility to call
	 * drm_crtc_vblank_put after this entry point returns 0, typically when
	 * the flip completes.
	 */
	int (*page_flip_target)(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event,
				uint32_t flags, uint32_t target,
				struct drm_modeset_acquire_ctx *ctx);

	/**
	 * @set_property:
	 *
	 * This is the legacy entry point to update a property attached to the
	 * CRTC.
	 *
	 * This callback is optional if the driver does not support any legacy
	 * driver-private properties. For atomic drivers it is not used because
	 * property handling is done entirely in the DRM core.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_property)(struct drm_crtc *crtc,
			    struct drm_property *property, uint64_t val);

	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current atomic state for this CRTC and return it.
	 * The core and helpers guarantee that any atomic state duplicated with
	 * this hook and still owned by the caller (i.e. not transferred to the
	 * driver by calling &drm_mode_config_funcs.atomic_commit) will be
	 * cleaned up by calling the @atomic_destroy_state hook in this
	 * structure.
	 *
	 * This callback is mandatory for atomic drivers.
	 *
	 * Atomic drivers which don't subclass &struct drm_crtc_state should use
	 * drm_atomic_helper_crtc_duplicate_state(). Drivers that subclass the
	 * state structure to extend it with driver-private state should use
	 * __drm_atomic_helper_crtc_duplicate_state() to make sure shared state is
	 * duplicated in a consistent fashion across drivers.
	 *
	 * It is an error to call this hook before &drm_crtc.state has been
	 * initialized correctly.
	 *
	 * NOTE:
	 *
	 * If the duplicate state references refcounted resources this hook must
	 * acquire a reference for each of them. The driver must release these
	 * references again in @atomic_destroy_state.
	 *
	 * RETURNS:
	 *
	 * Duplicated atomic state or NULL when the allocation failed.
	 */
	struct drm_crtc_state *(*atomic_duplicate_state)(struct drm_crtc *crtc);

	/**
	 * @atomic_destroy_state:
	 *
	 * Destroy a state duplicated with @atomic_duplicate_state and release
	 * or unreference all resources it references
	 *
	 * This callback is mandatory for atomic drivers.
	 */
	void (*atomic_destroy_state)(struct drm_crtc *crtc,
				     struct drm_crtc_state *state);

	/**
	 * @atomic_set_property:
	 *
	 * Decode a driver-private property value and store the decoded value
	 * into the passed-in state structure. Since the atomic core decodes all
	 * standardized properties (even for extensions beyond the core set of
	 * properties which might not be implemented by all drivers) this
	 * requires drivers to subclass the state structure.
	 *
	 * Such driver-private properties should really only be implemented for
	 * truly hardware/vendor specific state. Instead it is preferred to
	 * standardize atomic extension and decode the properties used to expose
	 * such an extension in the core.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_crtc_set_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * NOTE:
	 *
	 * This function is called in the state assembly phase of atomic
	 * modesets, which can be aborted for any reason (including on
	 * userspace's request to just check whether a configuration would be
	 * possible). Drivers MUST NOT touch any persistent state (hardware or
	 * software) or data structures except the passed in @state parameter.
	 *
	 * Also since userspace controls in which order properties are set this
	 * function must not do any input validation (since the state update is
	 * incomplete and hence likely inconsistent). Instead any such input
	 * validation must be done in the various atomic_check callbacks.
	 *
	 * RETURNS:
	 *
	 * 0 if the property has been found, -EINVAL if the property isn't
	 * implemented by the driver (which should never happen, the core only
	 * asks for properties attached to this CRTC). No other validation is
	 * allowed by the driver. The core already checks that the property
	 * value is within the range (integer, valid enum value, ...) the driver
	 * set when registering the property.
	 */
	int (*atomic_set_property)(struct drm_crtc *crtc,
				   struct drm_crtc_state *state,
				   struct drm_property *property,
				   uint64_t val);
	/**
	 * @atomic_get_property:
	 *
	 * Reads out the decoded driver-private property. This is used to
	 * implement the GETCRTC IOCTL.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_crtc_get_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the property isn't implemented by the
	 * driver (which should never happen, the core only asks for
	 * properties attached to this CRTC).
	 */
	int (*atomic_get_property)(struct drm_crtc *crtc,
				   const struct drm_crtc_state *state,
				   struct drm_property *property,
				   uint64_t *val);

	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the crtc like debugfs interfaces.
	 * It is called late in the driver load sequence from drm_dev_register().
	 * Everything added from this callback should be unregistered in
	 * the early_unregister callback.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_crtc *crtc);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the crtc from
	 * @late_register. It is called from drm_dev_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 */
	void (*early_unregister)(struct drm_crtc *crtc);

	/**
	 * @set_crc_source:
	 *
	 * Changes the source of CRC checksums of frames at the request of
	 * userspace, typically for testing purposes. The sources available are
	 * specific of each driver and a %NULL value indicates that CRC
	 * generation is to be switched off.
	 *
	 * When CRC generation is enabled, the driver should call
	 * drm_crtc_add_crc_entry() at each frame, providing any information
	 * that characterizes the frame contents in the crcN arguments, as
	 * provided from the configured source. Drivers must accept an "auto"
	 * source name that will select a default source for this CRTC.
	 *
	 * Note that "auto" can depend upon the current modeset configuration,
	 * e.g. it could pick an encoder or output specific CRC sampling point.
	 *
	 * This callback is optional if the driver does not support any CRC
	 * generation functionality.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_crc_source)(struct drm_crtc *crtc, const char *source);
	/**
	 * @verify_crc_source:
	 *
	 * verifies the source of CRC checksums of frames before setting the
	 * source for CRC and during crc open. Source parameter can be NULL
	 * while disabling crc source.
	 *
	 * This callback is optional if the driver does not support any CRC
	 * generation functionality.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*verify_crc_source)(struct drm_crtc *crtc, const char *source,
				 size_t *values_cnt);
	/**
	 * @get_crc_sources:
	 *
	 * Driver callback for getting a list of all the available sources for
	 * CRC generation. This callback depends upon verify_crc_source, So
	 * verify_crc_source callback should be implemented before implementing
	 * this. Driver can pass full list of available crc sources, this
	 * callback does the verification on each crc-source before passing it
	 * to userspace.
	 *
	 * This callback is optional if the driver does not support exporting of
	 * possible CRC sources list.
	 *
	 * RETURNS:
	 *
	 * a constant character pointer to the list of all the available CRC
	 * sources. On failure driver should return NULL. count should be
	 * updated with number of sources in list. if zero we don't process any
	 * source from the list.
	 */
	const char *const *(*get_crc_sources)(struct drm_crtc *crtc,
					      size_t *count);

	/**
	 * @atomic_print_state:
	 *
	 * If driver subclasses &struct drm_crtc_state, it should implement
	 * this optional hook for printing additional driver specific state.
	 *
	 * Do not call this directly, use drm_atomic_crtc_print_state()
	 * instead.
	 */
	void (*atomic_print_state)(struct drm_printer *p,
				   const struct drm_crtc_state *state);

	/**
	 * @get_vblank_counter:
	 *
	 * Driver callback for fetching a raw hardware vblank counter for the
	 * CRTC. It's meant to be used by new drivers as the replacement of
	 * &drm_driver.get_vblank_counter hook.
	 *
	 * This callback is optional. If a device doesn't have a hardware
	 * counter, the driver can simply leave the hook as NULL. The DRM core
	 * will account for missed vblank events while interrupts where disabled
	 * based on system timestamps.
	 *
	 * Wraparound handling and loss of events due to modesetting is dealt
	 * with in the DRM core code, as long as drivers call
	 * drm_crtc_vblank_off() and drm_crtc_vblank_on() when disabling or
	 * enabling a CRTC.
	 *
	 * See also &drm_device.vblank_disable_immediate and
	 * &drm_device.max_vblank_count.
	 *
	 * Returns:
	 *
	 * Raw vblank counter value.
	 */
	u32 (*get_vblank_counter)(struct drm_crtc *crtc);

	/**
	 * @enable_vblank:
	 *
	 * Enable vblank interrupts for the CRTC. It's meant to be used by
	 * new drivers as the replacement of &drm_driver.enable_vblank hook.
	 *
	 * Returns:
	 *
	 * Zero on success, appropriate errno if the vblank interrupt cannot
	 * be enabled.
	 */
	int (*enable_vblank)(struct drm_crtc *crtc);

	/**
	 * @disable_vblank:
	 *
	 * Disable vblank interrupts for the CRTC. It's meant to be used by
	 * new drivers as the replacement of &drm_driver.disable_vblank hook.
	 */
	void (*disable_vblank)(struct drm_crtc *crtc);
};

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)

/**
 * struct vop_dump_info - vop dump plane info structure
 *
 * Store plane info used to write display data to /data/vop_buf/
 *
 */
struct vop_dump_info {
	/* @win_id: vop hard win index */
	u8 win_id;
	/* @area_id: vop hard area index inside win */
	u8 area_id;
	/* @AFBC_flag: indicate the buffer compress by gpu or not */
	bool AFBC_flag;
	/* @yuv_format: indicate yuv format or not */
	bool yuv_format;
	/* @pitches: the buffer pitch size */
	u32 pitches;
	/* @height: the buffer pitch height */
	u32 height;
	/* @pixel_format: the buffer format */
	u32 pixel_format;
	/* @offset: the buffer offset */
	unsigned long offset;
	/* @num_pages: the pages number */
	unsigned long num_pages;
	/* @pages: store the buffer all pages */
	struct page **pages;
};

/**
 * struct vop_dump_list - store all buffer info per frame
 *
 * one frame maybe multiple buffer, all will be stored here.
 *
 */
struct vop_dump_list {
	struct list_head entry;
	struct vop_dump_info dump_info;
};

enum vop_dump_status {
	DUMP_DISABLE = 0,
	DUMP_KEEP
};
#endif

/**
 * struct drm_crtc - CRTC（显示控制器）核心控制结构体
 *
 * 【CRTC 是什么？】
 * CRTC（Cathode Ray Tube Controller，阴极射线管控制器）这个名字来自 CRT 时代，
 * 在现代显示系统中，它是"显示流水线的定时主控"：
 *   - 从帧缓冲（Framebuffer）中读取像素数据
 *   - 按照 display mode（分辨率、刷新率）产生精确的像素时钟和行/帧同步信号
 *   - 将像素流送往 Encoder → Connector → 物理显示设备
 *
 * 【在 Rockchip VOP2 中的对应关系】
 *   一个 drm_crtc = VOP2 中的一个 VP（Video Port）
 *   RK3568 有 VP0、VP1、VP2 三个 VP，因此有三个 drm_crtc。
 *   每个 VP 可以绑定不同的显示接口（HDMI、MIPI DSI、eDP 等）。
 *
 * 【显示流水线全貌】
 *
 *   Framebuffer（GEM buffer，物理内存中的像素数组）
 *        │ DMA 读取（VOP2 通过 IOMMU 从 DDR 搬运像素）
 *        ▼
 *   drm_plane（Win 图层，负责缩放、格式转换、Alpha 合成）
 *        │ 多图层合成
 *        ▼
 *   drm_crtc（VP，产生行/帧时序，控制整帧输出节奏）
 *        │ 像素流 + 同步信号
 *        ▼
 *   drm_encoder（将 CRTC 并行像素流转换为接口专用信号，如 TMDS/MIPI）
 *        │
 *        ▼
 *   drm_connector（物理接口，HDMI/DSI/eDP，连接真实显示器）
 *
 * 【原子提交与 CRTC state】
 * 现代驱动使用原子（atomic）模式：所有参数修改先写入 drm_crtc_state，
 * 通过 atomic_check 验证合法性，再通过 atomic_commit 一次性生效，
 * 保证"全成功或全失败"的事务语义。
 * 结构体中以"Should only be used by legacy drivers"标注的字段
 * 是为了兼容旧版 IOCTL，原子驱动应使用对应的 drm_crtc_state 字段。
 *
 * 【VBlank 与帧同步】
 * CRTC 驱动显示扫描，每扫完一帧进入垂直消隐期（VBlank）。
 * VBlank 是更新帧缓冲的安全窗口：此期间切换 framebuffer 不会产生撕裂。
 * Page flip、fence signal 等操作都在 VBlank 中完成。
 */
struct drm_crtc {
	/**
	 * @dev: 所属的 DRM 设备（struct drm_device）
	 * 通过此指针访问全局资源，如 mode_config、event_lock 等。
	 */
	struct drm_device *dev;

	/**
	 * @port: 设备树（Device Tree）中对应的 OF 节点
	 *
	 * 用于 drm_of_find_possible_crtcs()，通过解析 DTS 的 port/endpoint
	 * 拓扑确定哪些 encoder 可以连接到本 CRTC。
	 *
	 * 在 Rockchip VOP2 的 DTS 中，每个 VP 有一个 port 节点：
	 *   vop: vop@fe040000 {
	 *       vp0: port@0 { ... }   ← VP0 的 port，即 drm_crtc[0].port
	 *       vp1: port@1 { ... }   ← VP1 的 port，即 drm_crtc[1].port
	 *   }
	 */
	struct device_node *port;

	/**
	 * @head: 全局 CRTC 链表节点
	 *
	 * 链入 drm_mode_config.crtc_list，DRM 核心通过此链表遍历所有 CRTC。
	 * 在 drm_crtc_init_with_planes() 中初始化，设备生命周期内不变，无需加锁。
	 * 遍历宏：for_each_crtc(dev, crtc)
	 */
	struct list_head head;

	/**
	 * @name: CRTC 的可读名称（如 "crtc-0"）
	 *
	 * 用于 debugfs、日志输出等。驱动可在注册时覆盖默认名称，
	 * 例如 Rockchip 驱动会设为 "vp0"、"vp1" 等与硬件对应的名字。
	 */
	char *name;

	/**
	 * @mutex: CRTC 状态的读写锁（struct drm_modeset_lock，基于 ww_mutex）
	 *
	 * 【锁语义】
	 *   读锁（共享）：保护整体 CRTC 状态（mode、dpms 状态等）。
	 *   写锁（独占）：保护可在不触发完整 modeset 前提下更新的内容，
	 *                 如 framebuffer 切换（page flip）、cursor 位置、
	 *                 CRTC property 值等。
	 *
	 * 【完整 modeset 的加锁要求】
	 *   需要同时持有：
	 *     1. 本 CRTC 的 @mutex（写锁）
	 *     2. drm_mode_config.connection_mutex（全局连接拓扑锁）
	 *   因为 modeset 可能改变 encoder/connector 的连接关系。
	 *
	 * 【原子驱动的特殊说明】
	 *   @mutex 保护 @state 指针（当前 drm_crtc_state）。
	 *   非阻塞原子提交在硬件提交阶段不持有 @mutex，而是通过
	 *   drm_atomic_state 快照指针或严格操作顺序（drm_crtc_commit）安全访问。
	 */
	struct drm_modeset_lock mutex;

	/**
	 * @base: KMS 基类对象（struct drm_mode_object）
	 *
	 * 提供：唯一 ID（uint32_t）、对象类型（DRM_MODE_OBJECT_CRTC）、
	 * property 实例表、动态引用计数。
	 * 用户空间通过此 ID 在 IOCTL 中引用本 CRTC。
	 */
	struct drm_mode_object base;

	/**
	 * @primary: 主显示图层（Primary Plane）
	 *
	 * 仅供遗留 IOCTL 使用（SETCRTC、PAGE_FLIP），指定这两个 IOCTL
	 * 隐式操作的 plane。原子驱动应直接通过 drm_plane_state 操作 plane。
	 * 在 Rockchip VOP2 中，primary plane 对应 VP 的主 Win 图层（Win0）。
	 */
	struct drm_plane *primary;

	/**
	 * @cursor: 光标图层（Cursor Plane）
	 *
	 * 仅供遗留 IOCTL 使用（SETCURSOR/SETCURSOR2），指定光标 IOCTL
	 * 隐式操作的 plane。硬件光标是独立的小尺寸图层（如 64×64 ARGB），
	 * 由专用硬件叠加在画面顶层，响应延迟极低，无需 CPU 重绘整帧。
	 * 原子驱动应通过 drm_plane_state.crtc_x/y 获取光标坐标。
	 */
	struct drm_plane *cursor;

	/**
	 * @index: CRTC 在 mode_config.crtc_list 中的位置索引（从 0 起）
	 *
	 * 设备生命周期内不变，可直接用作数组下标，例如：
	 *   priv->vblank[crtc->index]   ← VBlank 管理数组
	 *   priv->vcnt[crtc->index]     ← Rockchip vcnt 事件槽位
	 * 与 drm_crtc_index(crtc) 返回值相同。
	 */
	unsigned index;

	/**
	 * @cursor_x / @cursor_y: 光标当前位置（遗留字段）
	 *
	 * 遗留 SETCURSOR IOCTL 只能更新 cursor framebuffer，无法同时更新坐标，
	 * 这两个字段用于在两次 IOCTL 间记忆光标位置。
	 * 原子驱动不应使用，应读取 cursor plane 的 drm_plane_state.crtc_x/y。
	 */
	int cursor_x;
	int cursor_y;

	/**
	 * @enabled: CRTC 是否处于启用状态（遗留字段）
	 *
	 * 仅供遗留驱动使用。原子驱动应查询：
	 *   drm_crtc_state.enable  ← CRTC 是否在显示链路中（逻辑启用）
	 *   drm_crtc_state.active  ← CRTC 是否实际输出信号（区分 DPMS 关闭）
	 * 由 drm_atomic_helper_update_legacy_modeset_state() 在提交后同步更新。
	 */
	bool enabled;

	/**
	 * @mode: 当前生效的显示模式（遗留字段）
	 *
	 * 描述显示时序：分辨率（hdisplay × vdisplay）、刷新率、像素时钟、
	 * 行/帧同步脉冲宽度和位置（HSync/VSync timing）等。
	 * 仅供遗留驱动使用，原子驱动应查询 drm_crtc_state.mode。
	 */
	struct drm_display_mode mode;

	/**
	 * @hwmode: 实际写入硬件寄存器的显示模式（遗留字段）
	 *
	 * 与 @mode 的区别：@hwmode 是经过 encoder、panel、CRTC 各种
	 * 缩放/补偿调整之后，最终编程进硬件的时序参数（如行数有 padding）。
	 *
	 * 用途：drm_calc_vbltimestamp_from_scanoutpos() 用此计算高精度
	 * VBlank 时间戳（需要精确的硬件行数和像素时钟）。
	 * 原子驱动应使用 drm_crtc_state.adjusted_mode；
	 * VBlank 时间戳计算请用 drm_vblank_crtc.hwmode。
	 */
	struct drm_display_mode hwmode;

	/**
	 * @x / @y: CRTC 输出在虚拟桌面坐标系中的起始坐标（遗留字段）
	 *
	 * 用于多屏拼接场景，表示本 CRTC 覆盖的区域在大桌面中的偏移。
	 * 原子驱动应查询 primary plane 的 drm_plane_state.crtc_x/y。
	 */
	int x;
	int y;

	/**
	 * @funcs: CRTC 操作函数表（struct drm_crtc_funcs）
	 *
	 * 驱动实现的核心回调，关键回调包括：
	 *   .enable_vblank()           ← 启用 VBlank 中断（VOP2: vop2_enable_vblank）
	 *   .disable_vblank()          ← 禁用 VBlank 中断
	 *   .get_vblank_counter()      ← 读取硬件帧计数器
	 *   .atomic_duplicate_state()  ← 复制 drm_crtc_state（原子提交前快照）
	 *   .atomic_destroy_state()    ← 销毁 drm_crtc_state
	 *   .destroy()                 ← CRTC 销毁时的资源清理
	 * Rockchip VOP2 在 vop2_crtc_funcs 中实现这些回调。
	 */
	const struct drm_crtc_funcs *funcs;

	/**
	 * @gamma_size: 遗留 Gamma LUT 的条目数（典型值：256）
	 *
	 * 由 drm_mode_crtc_set_gamma_size() 设置，告知用户空间
	 * 该 CRTC 支持的 Gamma 校正精度。
	 * 原子驱动应通过 GAMMA_LUT property（Blob 类型）传递 Gamma 表。
	 */
	uint32_t gamma_size;

	/**
	 * @gamma_store: 遗留 Gamma LUT 数据存储
	 *
	 * 存储用户空间通过 DRM_IOCTL_MODE_SETGAMMA 设置的 R/G/B 三通道 Gamma 值。
	 * 格式：[R0..R255, G0..G255, B0..B255]，共 gamma_size×3 个 uint16_t。
	 */
	uint16_t *gamma_store;

	/**
	 * @helper_private: 中间层（helper）私有数据（struct drm_crtc_helper_funcs）
	 *
	 * 包含 atomic commit 流程各阶段的 helper 回调：
	 *   .atomic_check()    ← 验证新状态合法性（检查 mode、plane 兼容性等）
	 *   .atomic_begin()    ← 开始硬件更新前的准备（如获取 VBlank 参考）
	 *   .atomic_flush()    ← 提交硬件更新，写入寄存器（通常在 VBlank 期间生效）
	 *   .atomic_enable()   ← 启用 CRTC 输出（VP 上电、PLL 启动）
	 *   .atomic_disable()  ← 关闭 CRTC 输出（VP 下电）
	 * Rockchip VOP2 在 vop2_crtc_helper_funcs 中实现。
	 */
	const struct drm_crtc_helper_funcs *helper_private;

	/**
	 * @properties: 该 CRTC 上挂载的 property 实例表
	 *
	 * 存储已附加到本 CRTC 的所有 drm_property 的实例值。
	 * 原子驱动中，可变 property 值存储在 drm_crtc_state 中，
	 * @properties 仅用于不可变（IMMUTABLE）property 或遗留接口。
	 * 用户空间通过 DRM_IOCTL_MODE_OBJ_GETPROPERTIES 枚举这些属性。
	 * 常见属性：ACTIVE、MODE_ID、OUT_FENCE_PTR、GAMMA_LUT、CTM 等。
	 */
	struct drm_object_properties properties;

	/**
	 * @state: 当前原子状态快照（struct drm_crtc_state）
	 *
	 * 【核心字段，必须理解】
	 *
	 * 指向本 CRTC 当前生效的原子状态，包含：
	 *   .enable        ← CRTC 是否在显示链路中（逻辑启用）
	 *   .active        ← CRTC 是否实际输出信号（区分 DPMS 关闭）
	 *   .mode          ← 当前显示模式（逻辑模式）
	 *   .adjusted_mode ← 实际写入硬件的模式
	 *   .plane_mask    ← 此 CRTC 使用的 plane 位掩码
	 *   .commit        ← 最近一次提交的 drm_crtc_commit 指针
	 *
	 * 【保护机制与无锁访问】
	 * 正常路径：@state 由 @mutex（写锁）保护。
	 *
	 * 非阻塞原子提交（工作队列中执行）不持有 @mutex，通过两种安全方式访问：
	 *
	 *   方式1：通过 drm_atomic_state 中的状态快照（check 阶段冻结，无竞争）：
	 *     for_each_oldnew_crtc_in_state(state, crtc, old_state, new_state, i) {
	 *         // old_state：提交前的状态（只读）
	 *         // new_state：提交后期望的状态（只读）
	 *     }
	 *
	 *   方式2：原子 helper 在 drm_atomic_helper_swap_state()（将 new_state
	 *           写入 @state）之前完成对旧状态的所有操作，之后访问新状态，
	 *           通过严格操作顺序避免竞争（见 struct drm_crtc_commit）。
	 */
	struct drm_crtc_state *state;

	/**
	 * @commit_list: 待处理提交的链表（struct drm_crtc_commit）
	 *
	 * 【drm_crtc_commit 的作用】
	 * 一次原子提交在本 CRTC 上经历三个里程碑（completion）：
	 *   flip_done    ← VBlank 触发，硬件已切换到新帧（page flip 完成）
	 *   hw_done      ← 硬件寄存器写入完成（可以开始下一次 check）
	 *   cleanup_done ← 旧 framebuffer 引用释放完毕（可以 unpin/free）
	 *
	 * @commit_list 挂的是所有"尚未走完 cleanup_done 阶段"的提交，
	 * 用于在旧 framebuffer 真正可以释放前进行等待，
	 * 防止 GPU 或 CRTC 还在读的 framebuffer 被提前释放。
	 *
	 * 访问提交历史的推荐方式：
	 *   - 若只需上一次提交：用 old_crtc_state->commit 指针，无需加锁
	 *   - @commit_list 仅用于等待 cleanup_done（framebuffer 清理同步）
	 *
	 * 受 @commit_lock（spinlock）保护。
	 */
	struct list_head commit_list;

	/**
	 * @commit_lock: 保护 @commit_list 的自旋锁
	 *
	 * 使用 spinlock 而非 mutex 的原因：
	 * framebuffer 清理（cleanup_done signal）可能在 VBlank 中断上下文中触发，
	 * spinlock 可在中断上下文中安全使用，mutex 不行。
	 */
	spinlock_t commit_lock;

#ifdef CONFIG_DEBUG_FS
	/**
	 * @debugfs_entry: 该 CRTC 的 debugfs 目录节点
	 *
	 * 挂载在 /sys/kernel/debug/dri/<card>/crtc-<N>/ 下，
	 * 提供 CRTC 状态、VBlank 统计、硬件寄存器 dump 等调试信息。
	 * Rockchip VOP2 驱动在此目录额外注册 vop_regs、underrun 等节点。
	 */
	struct dentry *debugfs_entry;
#endif

	/**
	 * @crc: CRC 捕获配置（struct drm_crtc_crc）
	 *
	 * 支持硬件 CRC 校验功能：VOP2/CRTC 对每帧输出数据计算 CRC 值，
	 * 内核将其上报到 debugfs，用户空间（如 IGT 测试框架）读取并验证
	 * 帧内容是否符合预期，实现"无像素截图的图像正确性验证"。
	 * 包含 source（CRC 来源选择）、entries（环形缓冲区）、wait_queue 等。
	 */
	struct drm_crtc_crc crc;

	/**
	 * @fence_context: CRTC out-fence timeline 的上下文 ID
	 *
	 * 每个 CRTC 维护一条独立的 fence timeline，追踪该 CRTC 上帧的完成顺序。
	 * @fence_context 由 dma_fence_context_alloc() 分配，全局唯一。
	 *
	 * 【OUT_FENCE_PTR property 的工作原理】
	 * 用户空间在 atomic commit 时可通过 OUT_FENCE_PTR property 请求 fence fd：
	 *   1. 内核用 @fence_context + @fence_seqno 创建一个 dma_fence
	 *   2. 将 fence 封装为 sync_file fd 返回给用户空间
	 *   3. CRTC 在 VBlank 完成本次 page flip 后 signal 此 fence
	 *   4. 用户空间等待该 fd，确认帧已上屏后再准备下一帧
	 * 这实现了"CRTC 上屏完成"的显式同步通知。
	 */
	unsigned int fence_context;

	/**
	 * @fence_lock: 保护 fence timeline 操作的自旋锁
	 *
	 * 保护基于 @fence_context + @fence_seqno 的 fence 创建操作，
	 * 防止并发创建时 seqno 不单调。
	 * 使用 spinlock 是因为 fence signal 在 VBlank 中断上下文中发生。
	 */
	spinlock_t fence_lock;

	/**
	 * @fence_seqno: CRTC fence timeline 的序列号（单调递增）
	 *
	 * 每次为本 CRTC 创建新的 out-fence 时 seqno 自增 1。
	 * seqno 较小的 fence 先被 signal（帧按顺序上屏），保证 timeline 顺序语义。
	 */
	unsigned long fence_seqno;

	/**
	 * @timeline_name: fence timeline 的可读名称（最长 31 字符）
	 *
	 * 通常格式为 "CRTC:<id>-<name>"，例如 "CRTC:31-vp0"。
	 * 在 /sys/kernel/debug/sync/info 或 sync_file 信息中可见，【笔记钩子】
	 * 方便调试时识别 fence 属于哪个 CRTC 的哪一帧。
	 */
	char timeline_name[32];

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	/**
	 * Rockchip VOP dump 调试功能（CONFIG_ROCKCHIP_DRM_DEBUG 专用）【笔记钩子】
	 *
	 * 提供在运行时抓取 VOP2 输出帧（raw pixel data）的调试能力，
	 * 类似"帧截图"，用于现场排查显示异常问题。
	 * 触发方式：echo 1 > /sys/kernel/debug/dri/0/vop_dump/dump
	 *
	 * @vop_dump_status:         dump 状态机（DUMP_DISABLE / DUMP_KEEP）
	 * @vop_dump_list_head:      已捕获帧 buffer 的链表头（struct vop_dump_list）
	 * @vop_dump_list_init_flag: 链表是否已初始化（防止重复初始化）
	 * @vop_dump_times:          期望 dump 的帧数（0 表示持续抓取）
	 * @frame_count:             已成功 dump 的帧计数
	 */
	enum vop_dump_status vop_dump_status;
	struct list_head vop_dump_list_head;
	bool vop_dump_list_init_flag;
	int vop_dump_times;
	int frame_count;
#endif
};

/**
 * struct drm_mode_set - new values for a CRTC config change
 * @fb: framebuffer to use for new config
 * @crtc: CRTC whose configuration we're about to change
 * @mode: mode timings to use
 * @x: position of this CRTC relative to @fb
 * @y: position of this CRTC relative to @fb
 * @connectors: array of connectors to drive with this CRTC if possible
 * @num_connectors: size of @connectors array
 *
 * This represents a modeset configuration for the legacy SETCRTC ioctl and is
 * also used internally. Atomic drivers instead use &drm_atomic_state.
 */
struct drm_mode_set {
	struct drm_framebuffer *fb;
	struct drm_crtc *crtc;
	struct drm_display_mode *mode;

	uint32_t x;
	uint32_t y;

	struct drm_connector **connectors;
	size_t num_connectors;
};

#define obj_to_crtc(x) container_of(x, struct drm_crtc, base)

__printf(6, 7)
int drm_crtc_init_with_planes(struct drm_device *dev,
			      struct drm_crtc *crtc,
			      struct drm_plane *primary,
			      struct drm_plane *cursor,
			      const struct drm_crtc_funcs *funcs,
			      const char *name, ...);
void drm_crtc_cleanup(struct drm_crtc *crtc);

/**
 * drm_crtc_index - find the index of a registered CRTC
 * @crtc: CRTC to find index for
 *
 * Given a registered CRTC, return the index of that CRTC within a DRM
 * device's list of CRTCs.
 */
static inline unsigned int drm_crtc_index(const struct drm_crtc *crtc)
{
	return crtc->index;
}

/**
 * drm_crtc_mask - find the mask of a registered CRTC
 * @crtc: CRTC to find mask for
 *
 * Given a registered CRTC, return the mask bit of that CRTC for the
 * &drm_encoder.possible_crtcs and &drm_plane.possible_crtcs fields.
 */
static inline uint32_t drm_crtc_mask(const struct drm_crtc *crtc)
{
	return 1 << drm_crtc_index(crtc);
}

int drm_crtc_force_disable(struct drm_crtc *crtc);
int drm_crtc_force_disable_all(struct drm_device *dev);

int drm_mode_set_config_internal(struct drm_mode_set *set);
struct drm_crtc *drm_crtc_from_index(struct drm_device *dev, int idx);

/**
 * drm_crtc_find - look up a CRTC object from its ID
 * @dev: DRM device
 * @file_priv: drm file to check for lease against.
 * @id: &drm_mode_object ID
 *
 * This can be used to look up a CRTC from its userspace ID. Only used by
 * drivers for legacy IOCTLs and interface, nowadays extensions to the KMS
 * userspace interface should be done using &drm_property.
 */
static inline struct drm_crtc *drm_crtc_find(struct drm_device *dev,
		struct drm_file *file_priv,
		uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, file_priv, id, DRM_MODE_OBJECT_CRTC);
	return mo ? obj_to_crtc(mo) : NULL;
}

/**
 * drm_for_each_crtc - iterate over all CRTCs
 * @crtc: a &struct drm_crtc as the loop cursor
 * @dev: the &struct drm_device
 *
 * Iterate over all CRTCs of @dev.
 */
#define drm_for_each_crtc(crtc, dev) \
	list_for_each_entry(crtc, &(dev)->mode_config.crtc_list, head)

#endif /* __DRM_CRTC_H__ */
