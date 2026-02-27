/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef __DRM_CONNECTOR_H__
#define __DRM_CONNECTOR_H__

#include <linux/list.h>
#include <linux/llist.h>
#include <linux/ctype.h>
#include <linux/hdmi.h>
#include <drm/drm_mode_object.h>

#include <uapi/drm/drm_mode.h>

struct drm_connector_helper_funcs;
struct drm_modeset_acquire_ctx;
struct drm_device;
struct drm_crtc;
struct drm_encoder;
struct drm_property;
struct drm_property_blob;
struct drm_printer;
struct drm_panel;
struct edid;

enum drm_connector_force {
	DRM_FORCE_UNSPECIFIED,
	DRM_FORCE_OFF,
	DRM_FORCE_ON,         /* force on analog part normally */
	DRM_FORCE_ON_DIGITAL, /* for DVI-I use digital connector */
};

/**
 * enum drm_connector_status - status for a &drm_connector
 *
 * This enum is used to track the connector status. There are no separate
 * #defines for the uapi!
 */
enum drm_connector_status {
	/**
	 * @connector_status_connected: The connector is definitely connected to
	 * a sink device, and can be enabled.
	 */
	connector_status_connected = 1,
	/**
	 * @connector_status_disconnected: The connector isn't connected to a
	 * sink device which can be autodetect. For digital outputs like DP or
	 * HDMI (which can be realiable probed) this means there's really
	 * nothing there. It is driver-dependent whether a connector with this
	 * status can be lit up or not.
	 */
	connector_status_disconnected = 2,
	/**
	 * @connector_status_unknown: The connector's status could not be
	 * reliably detected. This happens when probing would either cause
	 * flicker (like load-detection when the connector is in use), or when a
	 * hardware resource isn't available (like when load-detection needs a
	 * free CRTC). It should be possible to light up the connector with one
	 * of the listed fallback modes. For default configuration userspace
	 * should only try to light up connectors with unknown status when
	 * there's not connector with @connector_status_connected.
	 */
	connector_status_unknown = 3,
};

/**
 * enum drm_connector_registration_status - userspace registration status for
 * a &drm_connector
 *
 * This enum is used to track the status of initializing a connector and
 * registering it with userspace, so that DRM can prevent bogus modesets on
 * connectors that no longer exist.
 */
enum drm_connector_registration_state {
	/**
	 * @DRM_CONNECTOR_INITIALIZING: The connector has just been created,
	 * but has yet to be exposed to userspace. There should be no
	 * additional restrictions to how the state of this connector may be
	 * modified.
	 */
	DRM_CONNECTOR_INITIALIZING = 0,

	/**
	 * @DRM_CONNECTOR_REGISTERED: The connector has been fully initialized
	 * and registered with sysfs, as such it has been exposed to
	 * userspace. There should be no additional restrictions to how the
	 * state of this connector may be modified.
	 */
	DRM_CONNECTOR_REGISTERED = 1,

	/**
	 * @DRM_CONNECTOR_UNREGISTERED: The connector has either been exposed
	 * to userspace and has since been unregistered and removed from
	 * userspace, or the connector was unregistered before it had a chance
	 * to be exposed to userspace (e.g. still in the
	 * @DRM_CONNECTOR_INITIALIZING state). When a connector is
	 * unregistered, there are additional restrictions to how its state
	 * may be modified:
	 *
	 * - An unregistered connector may only have its DPMS changed from
	 *   On->Off. Once DPMS is changed to Off, it may not be switched back
	 *   to On.
	 * - Modesets are not allowed on unregistered connectors, unless they
	 *   would result in disabling its assigned CRTCs. This means
	 *   disabling a CRTC on an unregistered connector is OK, but enabling
	 *   one is not.
	 * - Removing a CRTC from an unregistered connector is OK, but new
	 *   CRTCs may never be assigned to an unregistered connector.
	 */
	DRM_CONNECTOR_UNREGISTERED = 2,
};

enum subpixel_order {
	SubPixelUnknown = 0,
	SubPixelHorizontalRGB,
	SubPixelHorizontalBGR,
	SubPixelVerticalRGB,
	SubPixelVerticalBGR,
	SubPixelNone,

};

/**
 * struct drm_scrambling: sink's scrambling support.
 */
struct drm_scrambling {
	/**
	 * @supported: scrambling supported for rates > 340 Mhz.
	 */
	bool supported;
	/**
	 * @low_rates: scrambling supported for rates <= 340 Mhz.
	 */
	bool low_rates;
};

/*
 * struct drm_scdc - Information about scdc capabilities of a HDMI 2.0 sink
 *
 * Provides SCDC register support and capabilities related information on a
 * HDMI 2.0 sink. In case of a HDMI 1.4 sink, all parameter must be 0.
 */
struct drm_scdc {
	/**
	 * @supported: status control & data channel present.
	 */
	bool supported;
	/**
	 * @read_request: sink is capable of generating scdc read request.
	 */
	bool read_request;
	/**
	 * @scrambling: sink's scrambling capabilities
	 */
	struct drm_scrambling scrambling;
};

/**
 * struct drm_hdmi_dsc_cap - DSC capabilities of HDMI sink
 *
 * Describes the DSC support provided by HDMI 2.1 sink.
 * The information is fetched fom additional HFVSDB blocks defined
 * for HDMI 2.1.
 */
struct drm_hdmi_dsc_cap {
	/** @v_1p2: flag for dsc1.2 version support by sink */
	bool v_1p2;

	/** @native_420: Does sink support DSC with 4:2:0 compression */
	bool native_420;

	/**
	 * @all_bpp: Does sink support all bpp with 4:4:4: or 4:2:2
	 * compressed formats
	 */
	bool all_bpp;

	/**
	 * @bpc_supported: compressed bpc supported by sink : 10, 12 or 16 bpc
	 */
	u8 bpc_supported;

	/** @max_slices: maximum number of Horizontal slices supported by */
	u8 max_slices;

	/** @clk_per_slice : max pixel clock in MHz supported per slice */
	int clk_per_slice;

	/** @max_lanes : dsc max lanes supported for Fixed rate Link training */
	u8 max_lanes;

	/** @max_frl_rate_per_lane : maximum frl rate with DSC per lane */
	u8 max_frl_rate_per_lane;

	/** @total_chunk_kbytes: max size of chunks in KBs supported per line*/
	u8 total_chunk_kbytes;
};

/**
 * struct drm_hdmi_info - runtime information about the connected HDMI sink
 *
 * Describes if a given display supports advanced HDMI 2.0 features.
 * This information is available in CEA-861-F extension blocks (like HF-VSDB).
 */
struct drm_hdmi_info {
	/** @scdc: sink's scdc support and capabilities */
	struct drm_scdc scdc;

	/**
	 * @y420_vdb_modes: bitmap of modes which can support ycbcr420
	 * output only (not normal RGB/YCBCR444/422 outputs). There are total
	 * 107 VICs defined by CEA-861-F spec, so the size is 128 bits to map
	 * upto 128 VICs;
	 */
	unsigned long y420_vdb_modes[BITS_TO_LONGS(128)];

	/**
	 * @y420_cmdb_modes: bitmap of modes which can support ycbcr420
	 * output also, along with normal HDMI outputs. There are total 107
	 * VICs defined by CEA-861-F spec, so the size is 128 bits to map upto
	 * 128 VICs;
	 */
	unsigned long y420_cmdb_modes[BITS_TO_LONGS(128)];

	/** @y420_cmdb_map: bitmap of SVD index, to extraxt vcb modes */
	u64 y420_cmdb_map;

	/** @y420_dc_modes: bitmap of deep color support index */
	u8 y420_dc_modes;

	/* @colorimetry: bitmap of supported colorimetry modes */
	u16 colorimetry;

	/** @max_frl_rate_per_lane: support fixed rate link */
	u8 max_frl_rate_per_lane;

	/** @max_lanes: supported by sink */
	u8 max_lanes;

	/** @dsc_cap: DSC capabilities of the sink */
	struct drm_hdmi_dsc_cap dsc_cap;
};

/**
 * enum drm_link_status - connector's link_status property value
 *
 * This enum is used as the connector's link status property value.
 * It is set to the values defined in uapi.
 *
 * @DRM_LINK_STATUS_GOOD: DP Link is Good as a result of successful
 *                        link training
 * @DRM_LINK_STATUS_BAD: DP Link is BAD as a result of link training
 *                       failure
 */
enum drm_link_status {
	DRM_LINK_STATUS_GOOD = DRM_MODE_LINK_STATUS_GOOD,
	DRM_LINK_STATUS_BAD = DRM_MODE_LINK_STATUS_BAD,
};

/**
 * enum drm_panel_orientation - panel_orientation info for &drm_display_info
 *
 * This enum is used to track the (LCD) panel orientation. There are no
 * separate #defines for the uapi!
 *
 * @DRM_MODE_PANEL_ORIENTATION_UNKNOWN: The drm driver has not provided any
 *					panel orientation information (normal
 *					for non panels) in this case the "panel
 *					orientation" connector prop will not be
 *					attached.
 * @DRM_MODE_PANEL_ORIENTATION_NORMAL:	The top side of the panel matches the
 *					top side of the device's casing.
 * @DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP: The top side of the panel matches the
 *					bottom side of the device's casing, iow
 *					the panel is mounted upside-down.
 * @DRM_MODE_PANEL_ORIENTATION_LEFT_UP:	The left side of the panel matches the
 *					top side of the device's casing.
 * @DRM_MODE_PANEL_ORIENTATION_RIGHT_UP: The right side of the panel matches the
 *					top side of the device's casing.
 */
enum drm_panel_orientation {
	DRM_MODE_PANEL_ORIENTATION_UNKNOWN = -1,
	DRM_MODE_PANEL_ORIENTATION_NORMAL = 0,
	DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP,
	DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
	DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

/*
 * This is a consolidated colorimetry list supported by HDMI and
 * DP protocol standard. The respective connectors will register
 * a property with the subset of this list (supported by that
 * respective protocol). Userspace will set the colorspace through
 * a colorspace property which will be created and exposed to
 * userspace.
 */

/* For Default case, driver will set the colorspace */
#define DRM_MODE_COLORIMETRY_DEFAULT			0
/* CEA 861 Normal Colorimetry options */
#define DRM_MODE_COLORIMETRY_NO_DATA			0
#define DRM_MODE_COLORIMETRY_SMPTE_170M_YCC		1
#define DRM_MODE_COLORIMETRY_BT709_YCC			2
/* CEA 861 Extended Colorimetry Options */
#define DRM_MODE_COLORIMETRY_XVYCC_601			3
#define DRM_MODE_COLORIMETRY_XVYCC_709			4
#define DRM_MODE_COLORIMETRY_SYCC_601			5
#define DRM_MODE_COLORIMETRY_OPYCC_601			6
#define DRM_MODE_COLORIMETRY_OPRGB			7
#define DRM_MODE_COLORIMETRY_BT2020_CYCC		8
#define DRM_MODE_COLORIMETRY_BT2020_RGB			9
#define DRM_MODE_COLORIMETRY_BT2020_YCC			10
/* Additional Colorimetry extension added as part of CTA 861.G */
#define DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65		11
#define DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER		12
/* DP MSA Colorimetry Options */
#define DRM_MODE_DP_COLORIMETRY_BT601_YCC		13
#define DRM_MODE_DP_COLORIMETRY_BT709_YCC		14
#define DRM_MODE_DP_COLORIMETRY_SRGB			15
#define DRM_MODE_DP_COLORIMETRY_RGB_WIDE_GAMUT		16
#define DRM_MODE_DP_COLORIMETRY_SCRGB			17

/**
 * struct drm_display_info - runtime data about the connected sink
 *
 * Describes a given display (e.g. CRT or flat panel) and its limitations. For
 * fixed display sinks like built-in panels there's not much difference between
 * this and &struct drm_connector. But for sinks with a real cable this
 * structure is meant to describe all the things at the other end of the cable.
 *
 * For sinks which provide an EDID this can be filled out by calling
 * drm_add_edid_modes().
 */
struct drm_display_info {
	/**
	 * @name: Name of the display.
	 */
	char name[DRM_DISPLAY_INFO_LEN];

	/**
	 * @width_mm: Physical width in mm.
	 */
        unsigned int width_mm;
	/**
	 * @height_mm: Physical height in mm.
	 */
	unsigned int height_mm;

	/**
	 * @pixel_clock: Maximum pixel clock supported by the sink, in units of
	 * 100Hz. This mismatches the clock in &drm_display_mode (which is in
	 * kHZ), because that's what the EDID uses as base unit.
	 */
	unsigned int pixel_clock;
	/**
	 * @bpc: Maximum bits per color channel. Used by HDMI and DP outputs.
	 */
	unsigned int bpc;

	/**
	 * @subpixel_order: Subpixel order of LCD panels.
	 */
	enum subpixel_order subpixel_order;

#define DRM_COLOR_FORMAT_RGB444		(1<<0)
#define DRM_COLOR_FORMAT_YCRCB444	(1<<1)
#define DRM_COLOR_FORMAT_YCRCB422	(1<<2)
#define DRM_COLOR_FORMAT_YCRCB420	(1<<3)

	/**
	 * @panel_orientation: Read only connector property for built-in panels,
	 * indicating the orientation of the panel vs the device's casing.
	 * drm_connector_init() sets this to DRM_MODE_PANEL_ORIENTATION_UNKNOWN.
	 * When not UNKNOWN this gets used by the drm_fb_helpers to rotate the
	 * fb to compensate and gets exported as prop to userspace.
	 */
	int panel_orientation;

	/**
	 * @color_formats: HDMI Color formats, selects between RGB and YCrCb
	 * modes. Used DRM_COLOR_FORMAT\_ defines, which are _not_ the same ones
	 * as used to describe the pixel format in framebuffers, and also don't
	 * match the formats in @bus_formats which are shared with v4l.
	 */
	u32 color_formats;

	/**
	 * @bus_formats: Pixel data format on the wire, somewhat redundant with
	 * @color_formats. Array of size @num_bus_formats encoded using
	 * MEDIA_BUS_FMT\_ defines shared with v4l and media drivers.
	 */
	const u32 *bus_formats;
	/**
	 * @num_bus_formats: Size of @bus_formats array.
	 */
	unsigned int num_bus_formats;

#define DRM_BUS_FLAG_DE_LOW		(1<<0)
#define DRM_BUS_FLAG_DE_HIGH		(1<<1)

/*
 * Don't use those two flags directly, use the DRM_BUS_FLAG_PIXDATA_DRIVE_*
 * and DRM_BUS_FLAG_PIXDATA_SAMPLE_* variants to qualify the flags explicitly.
 * The DRM_BUS_FLAG_PIXDATA_SAMPLE_* flags are defined as the opposite of the
 * DRM_BUS_FLAG_PIXDATA_DRIVE_* flags to make code simpler, as signals are
 * usually to be sampled on the opposite edge of the driving edge.
 */
#define DRM_BUS_FLAG_PIXDATA_POSEDGE	(1<<2)
#define DRM_BUS_FLAG_PIXDATA_NEGEDGE	(1<<3)

/* Drive data on rising edge */
#define DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE	DRM_BUS_FLAG_PIXDATA_POSEDGE
/* Drive data on falling edge */
#define DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE	DRM_BUS_FLAG_PIXDATA_NEGEDGE
/* Sample data on rising edge */
#define DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE	DRM_BUS_FLAG_PIXDATA_NEGEDGE
/* Sample data on falling edge */
#define DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE	DRM_BUS_FLAG_PIXDATA_POSEDGE

/* data is transmitted MSB to LSB on the bus */
#define DRM_BUS_FLAG_DATA_MSB_TO_LSB	(1<<4)
/* data is transmitted LSB to MSB on the bus */
#define DRM_BUS_FLAG_DATA_LSB_TO_MSB	(1<<5)

/*
 * Similarly to the DRM_BUS_FLAG_PIXDATA_* flags, don't use these two flags
 * directly, use one of the DRM_BUS_FLAG_SYNC_(DRIVE|SAMPLE)_* instead.
 */
#define DRM_BUS_FLAG_SYNC_POSEDGE	(1<<6)
#define DRM_BUS_FLAG_SYNC_NEGEDGE	(1<<7)

/* Drive sync on rising edge */
#define DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE		DRM_BUS_FLAG_SYNC_POSEDGE
/* Drive sync on falling edge */
#define DRM_BUS_FLAG_SYNC_DRIVE_NEGEDGE		DRM_BUS_FLAG_SYNC_NEGEDGE
/* Sample sync on rising edge */
#define DRM_BUS_FLAG_SYNC_SAMPLE_POSEDGE	DRM_BUS_FLAG_SYNC_NEGEDGE
/* Sample sync on falling edge */
#define DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE	DRM_BUS_FLAG_SYNC_POSEDGE

	/**
	 * @bus_flags: Additional information (like pixel signal polarity) for
	 * the pixel data on the bus, using DRM_BUS_FLAGS\_ defines.
	 */
	u32 bus_flags;

	/**
	 * @max_tmds_clock: Maximum TMDS clock rate supported by the
	 * sink in kHz. 0 means undefined.
	 */
	int max_tmds_clock;

	/**
	 * @dvi_dual: Dual-link DVI sink?
	 */
	bool dvi_dual;

	/**
	 * @has_hdmi_infoframe: Does the sink support the HDMI infoframe?
	 */
	bool has_hdmi_infoframe;

	/**
	 * @edid_hdmi_dc_modes: Mask of supported hdmi deep color modes. Even
	 * more stuff redundant with @bus_formats.
	 */
	u8 edid_hdmi_dc_modes;

	/**
	 * @cea_rev: CEA revision of the HDMI sink.
	 */
	u8 cea_rev;

	/**
	 * @hdmi: advance features of a HDMI sink.
	 */
	struct drm_hdmi_info hdmi;

	/**
	 * @non_desktop: Non desktop display (HMD).
	 */
	bool non_desktop;
};

int drm_display_info_set_bus_formats(struct drm_display_info *info,
				     const u32 *formats,
				     unsigned int num_formats);

/**
 * struct drm_tv_connector_state - TV connector related states
 * @subconnector: selected subconnector
 * @margins: margins
 * @margins.left: left margin
 * @margins.right: right margin
 * @margins.top: top margin
 * @margins.bottom: bottom margin
 * @mode: TV mode
 * @brightness: brightness in percent
 * @contrast: contrast in percent
 * @flicker_reduction: flicker reduction in percent
 * @overscan: overscan in percent
 * @saturation: saturation in percent
 * @hue: hue in percent
 */
struct drm_tv_connector_state {
	enum drm_mode_subconnector subconnector;
	struct {
		unsigned int left;
		unsigned int right;
		unsigned int top;
		unsigned int bottom;
	} margins;
	unsigned int mode;
	unsigned int brightness;
	unsigned int contrast;
	unsigned int flicker_reduction;
	unsigned int overscan;
	unsigned int saturation;
	unsigned int hue;
};

/**
 * struct drm_connector_state - mutable connector state
 */
struct drm_connector_state {
	/** @connector: backpointer to the connector */
	struct drm_connector *connector;

	/**
	 * @crtc: CRTC to connect connector to, NULL if disabled.
	 *
	 * Do not change this directly, use drm_atomic_set_crtc_for_connector()
	 * instead.
	 */
	struct drm_crtc *crtc;

	/**
	 * @best_encoder:
	 *
	 * Used by the atomic helpers to select the encoder, through the
	 * &drm_connector_helper_funcs.atomic_best_encoder or
	 * &drm_connector_helper_funcs.best_encoder callbacks.
	 */
	struct drm_encoder *best_encoder;

	/**
	 * @link_status: Connector link_status to keep track of whether link is
	 * GOOD or BAD to notify userspace if retraining is necessary.
	 */
	enum drm_link_status link_status;

	/** @state: backpointer to global drm_atomic_state */
	struct drm_atomic_state *state;

	/**
	 * @commit: Tracks the pending commit to prevent use-after-free conditions.
	 *
	 * Is only set when @crtc is NULL.
	 */
	struct drm_crtc_commit *commit;

	/** @tv: TV connector state */
	struct drm_tv_connector_state tv;

	/**
	 * @picture_aspect_ratio: Connector property to control the
	 * HDMI infoframe aspect ratio setting.
	 *
	 * The %DRM_MODE_PICTURE_ASPECT_\* values much match the
	 * values for &enum hdmi_picture_aspect
	 */
	enum hdmi_picture_aspect picture_aspect_ratio;

	/**
	 * @content_type: Connector property to control the
	 * HDMI infoframe content type setting.
	 * The %DRM_MODE_CONTENT_TYPE_\* values much
	 * match the values.
	 */
	unsigned int content_type;

	/**
	 * @scaling_mode: Connector property to control the
	 * upscaling, mostly used for built-in panels.
	 */
	unsigned int scaling_mode;

	/**
	 * @content_protection: Connector property to request content
	 * protection. This is most commonly used for HDCP.
	 */
	unsigned int content_protection;

	/**
	 * @colorspace: State variable for Connector property to request
	 * colorspace change on Sink. This is most commonly used to switch
	 * to wider color gamuts like BT2020.
	 */
	u32 colorspace;

	/**
	 * @writeback_job: Writeback job for writeback connectors
	 *
	 * Holds the framebuffer and out-fence for a writeback connector. As
	 * the writeback completion may be asynchronous to the normal commit
	 * cycle, the writeback job lifetime is managed separately from the
	 * normal atomic state by this object.
	 *
	 * See also: drm_writeback_queue_job() and
	 * drm_writeback_signal_completion()
	 */
	struct drm_writeback_job *writeback_job;

	/**
	 * @hdr_output_metadata:
	 * DRM blob property for HDR output metadata
	 */
	struct drm_property_blob *hdr_output_metadata;

	struct drm_property_blob *hdr_panel_blob_ptr;
};

/**
 * struct drm_connector_funcs - control connectors on a given device
 *
 * Each CRTC may have one or more connectors attached to it.  The functions
 * below allow the core DRM code to control connectors, enumerate available modes,
 * etc.
 */
struct drm_connector_funcs {
	/**
	 * @dpms:
	 *
	 * Legacy entry point to set the per-connector DPMS state. Legacy DPMS
	 * is exposed as a standard property on the connector, but diverted to
	 * this callback in the drm core. Note that atomic drivers don't
	 * implement the 4 level DPMS support on the connector any more, but
	 * instead only have an on/off "ACTIVE" property on the CRTC object.
	 *
	 * This hook is not used by atomic drivers, remapping of the legacy DPMS
	 * property is entirely handled in the DRM core.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*dpms)(struct drm_connector *connector, int mode);

	/**
	 * @reset:
	 *
	 * Reset connector hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 *
	 * Atomic drivers can use drm_atomic_helper_connector_reset() to reset
	 * atomic state using this hook.
	 */
	void (*reset)(struct drm_connector *connector);

	/**
	 * @detect:
	 *
	 * Check to see if anything is attached to the connector. The parameter
	 * force is set to false whilst polling, true when checking the
	 * connector due to a user request. force can be used by the driver to
	 * avoid expensive, destructive operations during automated probing.
	 *
	 * This callback is optional, if not implemented the connector will be
	 * considered as always being attached.
	 *
	 * FIXME:
	 *
	 * Note that this hook is only called by the probe helper. It's not in
	 * the helper library vtable purely for historical reasons. The only DRM
	 * core	entry point to probe connector state is @fill_modes.
	 *
	 * Note that the helper library will already hold
	 * &drm_mode_config.connection_mutex. Drivers which need to grab additional
	 * locks to avoid races with concurrent modeset changes need to use
	 * &drm_connector_helper_funcs.detect_ctx instead.
	 *
	 * RETURNS:
	 *
	 * drm_connector_status indicating the connector's status.
	 */
	enum drm_connector_status (*detect)(struct drm_connector *connector,
					    bool force);

	/**
	 * @force:
	 *
	 * This function is called to update internal encoder state when the
	 * connector is forced to a certain state by userspace, either through
	 * the sysfs interfaces or on the kernel cmdline. In that case the
	 * @detect callback isn't called.
	 *
	 * FIXME:
	 *
	 * Note that this hook is only called by the probe helper. It's not in
	 * the helper library vtable purely for historical reasons. The only DRM
	 * core	entry point to probe connector state is @fill_modes.
	 */
	void (*force)(struct drm_connector *connector);

	/**
	 * @fill_modes:
	 *
	 * Entry point for output detection and basic mode validation. The
	 * driver should reprobe the output if needed (e.g. when hotplug
	 * handling is unreliable), add all detected modes to &drm_connector.modes
	 * and filter out any the device can't support in any configuration. It
	 * also needs to filter out any modes wider or higher than the
	 * parameters max_width and max_height indicate.
	 *
	 * The drivers must also prune any modes no longer valid from
	 * &drm_connector.modes. Furthermore it must update
	 * &drm_connector.status and &drm_connector.edid.  If no EDID has been
	 * received for this output connector->edid must be NULL.
	 *
	 * Drivers using the probe helpers should use
	 * drm_helper_probe_single_connector_modes() to implement this
	 * function.
	 *
	 * RETURNS:
	 *
	 * The number of modes detected and filled into &drm_connector.modes.
	 */
	int (*fill_modes)(struct drm_connector *connector, uint32_t max_width, uint32_t max_height);

	/**
	 * @set_property:
	 *
	 * This is the legacy entry point to update a property attached to the
	 * connector.
	 *
	 * This callback is optional if the driver does not support any legacy
	 * driver-private properties. For atomic drivers it is not used because
	 * property handling is done entirely in the DRM core.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_property)(struct drm_connector *connector, struct drm_property *property,
			     uint64_t val);

	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the connector, light backlight control, i2c,
	 * DP aux or similar interfaces. It is called late in the driver load
	 * sequence from drm_connector_register() when registering all the
	 * core drm connector interfaces. Everything added from this callback
	 * should be unregistered in the early_unregister callback.
	 *
	 * This is called while holding &drm_connector.mutex.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_connector *connector);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the connector from
	 * late_register(). It is called from drm_connector_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 *
	 * This is called while holding &drm_connector.mutex.
	 */
	void (*early_unregister)(struct drm_connector *connector);

	/**
	 * @destroy:
	 *
	 * Clean up connector resources. This is called at driver unload time
	 * through drm_mode_config_cleanup(). It can also be called at runtime
	 * when a connector is being hot-unplugged for drivers that support
	 * connector hotplugging (e.g. DisplayPort MST).
	 */
	void (*destroy)(struct drm_connector *connector);

	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current atomic state for this connector and return it.
	 * The core and helpers guarantee that any atomic state duplicated with
	 * this hook and still owned by the caller (i.e. not transferred to the
	 * driver by calling &drm_mode_config_funcs.atomic_commit) will be
	 * cleaned up by calling the @atomic_destroy_state hook in this
	 * structure.
	 *
	 * This callback is mandatory for atomic drivers.
	 *
	 * Atomic drivers which don't subclass &struct drm_connector_state should use
	 * drm_atomic_helper_connector_duplicate_state(). Drivers that subclass the
	 * state structure to extend it with driver-private state should use
	 * __drm_atomic_helper_connector_duplicate_state() to make sure shared state is
	 * duplicated in a consistent fashion across drivers.
	 *
	 * It is an error to call this hook before &drm_connector.state has been
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
	struct drm_connector_state *(*atomic_duplicate_state)(struct drm_connector *connector);

	/**
	 * @atomic_destroy_state:
	 *
	 * Destroy a state duplicated with @atomic_duplicate_state and release
	 * or unreference all resources it references
	 *
	 * This callback is mandatory for atomic drivers.
	 */
	void (*atomic_destroy_state)(struct drm_connector *connector,
				     struct drm_connector_state *state);

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
	 * drm_atomic_connector_set_property() instead.
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
	 * implemented by the driver (which shouldn't ever happen, the core only
	 * asks for properties attached to this connector). No other validation
	 * is allowed by the driver. The core already checks that the property
	 * value is within the range (integer, valid enum value, ...) the driver
	 * set when registering the property.
	 */
	int (*atomic_set_property)(struct drm_connector *connector,
				   struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t val);

	/**
	 * @atomic_get_property:
	 *
	 * Reads out the decoded driver-private property. This is used to
	 * implement the GETCONNECTOR IOCTL.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_connector_get_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the property isn't implemented by the
	 * driver (which shouldn't ever happen, the core only asks for
	 * properties attached to this connector).
	 */
	int (*atomic_get_property)(struct drm_connector *connector,
				   const struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t *val);

	/**
	 * @atomic_print_state:
	 *
	 * If driver subclasses &struct drm_connector_state, it should implement
	 * this optional hook for printing additional driver specific state.
	 *
	 * Do not call this directly, use drm_atomic_connector_print_state()
	 * instead.
	 */
	void (*atomic_print_state)(struct drm_printer *p,
				   const struct drm_connector_state *state);
};

/* mode specified on the command line */
struct drm_cmdline_mode {
	bool specified;
	bool refresh_specified;
	bool bpp_specified;
	int xres, yres;
	int bpp;
	int refresh;
	bool rb;
	bool interlace;
	bool cvt;
	bool margins;
	enum drm_connector_force force;
};

/**
 * struct drm_connector - DRM 连接器（Connector）核心控制结构体
 *
 * Connector 是 DRM 显示管道中的**边境口岸**——芯片内部数字世界与外部物理显示设备
 * 之间的唯一法定边界。
 *
 * ## 在显示管道中的位置
 *
 *   Framebuffer → Plane → CRTC → Encoder → [Bridge] → **Connector** → Panel/显示器
 *                                   ↑                       ↑              ↑
 *                              时序主权方              物理边界口岸      最终发光端
 *
 * ## 与上游组件的关系
 *
 * ### Encoder（上游，信号编码铸造厂）
 *   - Encoder 把 CRTC 输出的并行 RGB 像素流，编码为高速串行差分信号
 *     （如 HDMI 的 TMDS、DP 的 ANSI 8b/10b）
 *   - 一个 Connector 可关联多个备选 Encoder（存储在 @encoder_ids 数组中），
 *     但同一时刻只能由一个 Encoder 驱动（@encoder 指针）
 *   - Encoder 的输出协议必须与 Connector 的物理接口类型匹配：
 *     TMDS Encoder → HDMI Connector，DSI Encoder → DSI Connector
 *
 * ### Bridge（可选，协议转换枢纽）
 *   - 当 Encoder 输出协议与 Connector 接口不匹配时，中间会插入 Bridge
 *     例如：DSI Encoder → DSI-to-eDP Bridge → eDP Connector
 *   - Bridge 只做协议翻译，不修改 CRTC 定下的时序规则
 *   - Bridge 对 Connector 透明——Connector 不关心信号是直连还是经过转换
 *
 * ## 与下游组件的关系
 *
 * ### Panel（下游，最终发光端）
 *   - 对于内嵌面板（DSI/eDP/LVDS），Connector 通过 @panel 指针关联到 drm_panel，
 *     调用 panel 的 prepare()/enable()/disable()/unprepare() 生命周期回调，
 *     控制面板的上电时序、初始化命令、背光开关
 *   - 对于外部显示器（HDMI/DP），没有 drm_panel 对象，Connector 通过 DDC 通道
 *     （I2C 总线）读取显示器的 EDID 获知其能力，显示器自行管理上电和初始化
 *
 * ## Connector 自身的核心职责
 *
 * Connector 不做任何信号编码/解码/放大，它的使命是：
 *
 * 1. **物理连接**：提供金属弹片/焊盘，将差分信号无损传递给屏幕
 * 2. **热插拔检测**：通过 HPD 引脚感知设备插入/拔出，触发中断通知内核（@polled）
 * 3. **设备身份核验**：通过 DDC 通道读取 EDID（@edid_blob_ptr），
 *    获取屏幕支持的分辨率、刷新率、色域、HDR 能力等，
 *    这些信息会反向流回上游，决定 CRTC 的时序配置
 * 4. **状态上报**：向用户空间暴露连接状态（@status）、显示模式（@modes）、
 *    属性（@properties）等信息
 *
 * ## 信息的反向流动（EDID → CRTC）
 *
 * 虽然信号是从 CRTC 单向流向 Connector，但**配置信息**是反向流动的：
 *   Connector 读取 EDID → 解析出屏幕支持的时序参数 →
 *   驱动据此配置 CRTC 的像素时钟、HSYNC/VSYNC 时序 →
 *   CRTC 的输出严格遵循屏幕声称能支持的标准参数
 *
 * 这就是为什么不同的屏幕需要不同的 CRTC 配置——CRTC 从来不会凭空定时序，
 * 它只会忠实执行屏幕 EDID 里写的参数。
 */
struct drm_connector {
	/**
	 * @dev: 父 DRM 设备（drm_device）。
	 * 所有 DRM 对象都通过此字段反向找到所属设备。
	 */
	struct drm_device *dev;

	/**
	 * @kdev: 对应的内核 device 对象，用于 sysfs 属性暴露。
	 * 用户空间可通过 /sys/class/drm/cardX-<connector>/ 访问连接器信息。
	 */
	struct device *kdev;

	/**
	 * @attr: sysfs 属性描述符。
	 * 定义了此连接器在 sysfs 中暴露的属性列表（如状态、模式等）。
	 */
	struct device_attribute *attr;

	/**
	 * @head: 链表节点，将此 connector 挂入 drm_mode_config.connector_list。
	 *
	 * 受 drm_mode_config.connector_list_lock 保护。
	 * 遍历时必须使用 drm_connector_list_iter，不能直接操作链表，
	 * 以正确处理热插拔时连接器的并发注销。
	 */
	struct list_head head;

	/**
	 * @base: DRM 模式对象基类（drm_mode_object）。
	 * 包含用户空间可见的唯一 ID、对象类型标识和属性链表。
	 * 通过 obj_to_connector(x) 可从基类指针反推到 drm_connector。
	 */
	struct drm_mode_object base;

	/**
	 * @name: 连接器的人类可读名称，例如 "HDMI-A-1"、"DSI-1"、"eDP-1"。
	 * 由 drm_connector_init() 根据类型和编号自动生成，驱动可覆盖。
	 * 这个名字会直接出现在 /sys/class/drm/ 和用户空间工具（如 modetest）中。
	 */
	char *name;

	/**
	 * @mutex: 连接器通用锁。
	 *
	 * 目前主要保护 @registration_state 字段。
	 * 大多数连接器状态仍由 drm_mode_config.mutex 保护。
	 *
	 * 锁层级：drm_mode_config.mutex > connector->mutex
	 */
	struct mutex mutex;

	/**
	 * @index: 连接器在 mode_config.connector_list 中的紧凑索引。
	 *
	 * 对不支持运行时热添加/移除的驱动，此值等于链表位置。
	 * 可用作数组下标，在连接器生命周期内保持不变。
	 * 通过 drm_connector_index(connector) 获取。
	 */
	unsigned index;

	/**
	 * @connector_type: 连接器物理接口类型。
	 *
	 * drm_mode.h 中 DRM_MODE_CONNECTOR_<foo> 枚举值之一，例如：
	 *   DRM_MODE_CONNECTOR_HDMIA   → HDMI A 型接口
	 *   DRM_MODE_CONNECTOR_DSI     → MIPI DSI 内嵌面板
	 *   DRM_MODE_CONNECTOR_eDP     → eDP 内嵌 DisplayPort
	 *   DRM_MODE_CONNECTOR_DisplayPort → 外接 DP
	 *   DRM_MODE_CONNECTOR_VGA     → VGA 模拟接口
	 */
	int connector_type;

	/**
	 * @connector_type_id: 同类型连接器的编号（从 1 开始）。
	 *
	 * 例如系统有两个 HDMI 口：
	 *   connector_type = DRM_MODE_CONNECTOR_HDMIA, connector_type_id = 1 → "HDMI-A-1"
	 *   connector_type = DRM_MODE_CONNECTOR_HDMIA, connector_type_id = 2 → "HDMI-A-2"
	 */
	int connector_type_id;

	/**
	 * @interlace_allowed: 此连接器是否支持逐行扫描模式（隔行扫描）。
	 *
	 * 隔行扫描（Interlaced）：每帧只扫描奇数行或偶数行，用于老式 TV 信号（如 1080i）。
	 * 现代显示器通常不需要，设为 false 可过滤掉所有隔行模式。
	 * 仅由 drm_helper_probe_single_connector_modes() 在模式过滤时使用。
	 */
	bool interlace_allowed;

	/**
	 * @doublescan_allowed: 此连接器是否支持双扫描（doublescan）模式。
	 *
	 * 双扫描：每行像素扫描两次，用于老式低分辨率显示模式（如 VGA 320x240）。
	 * 现代驱动几乎都设为 false。
	 * 仅由 drm_helper_probe_single_connector_modes() 在模式过滤时使用。
	 */
	bool doublescan_allowed;

	/**
	 * @stereo_allowed: 此连接器是否支持立体（3D）显示模式。
	 *
	 * 立体模式包括左右分屏、上下分屏、帧序列等 3D 格式（HDMI 1.4 3D、DP 3D）。
	 * 仅由 drm_helper_probe_single_connector_modes() 在模式过滤时使用。
	 */
	bool stereo_allowed;

	/**
	 * @ycbcr_420_allowed: 此连接器是否支持 YCbCr 4:2:0 色彩子采样输出。
	 *
	 * YCbCr 4:2:0：色度信息水平和垂直各下采样 2 倍，带宽只需 RGB 的一半。
	 * HDMI 2.0 的 4K@60Hz 就常用此格式（满足带宽限制）。
	 * 解析 EDID 时，驱动用此标志决定是否向显示器提供 YCbCr 4:2:0 模式。
	 */
	bool ycbcr_420_allowed;

	/**
	 * @registration_state: 连接器的注册状态。
	 *
	 * 三种状态：
	 *   DRM_CONNECTOR_INITIALIZING  → 正在初始化，用户空间还看不到
	 *   DRM_CONNECTOR_REGISTERED    → 已注册，用户空间可通过 ioctl 访问
	 *   DRM_CONNECTOR_UNREGISTERED  → 已注销（热拔后），用户空间不应再访问
	 *
	 * 受 @mutex 保护。
	 */
	enum drm_connector_registration_state registration_state;

	/**
	 * @modes: 此连接器当前可用的显示模式链表。
	 *
	 * 来源：fill_modes() 从 EDID 探测 + 用户通过 debugfs 手动添加。
	 * 包含分辨率、刷新率、时序参数等（drm_display_mode）。
	 * 受 drm_mode_config.mutex 保护。
	 */
	struct list_head modes;

	/**
	 * @status: 连接器的物理连接状态。
	 *
	 * 三种状态：
	 *   connector_status_connected    → 有显示器接入
	 *   connector_status_disconnected → 无显示器
	 *   connector_status_unknown      → 无法确定（如某些内嵌屏）
	 *
	 * 受 drm_mode_config.mutex 保护。
	 * 通过 drm_connector_funcs.detect() 回调更新。
	 */
	enum drm_connector_status status;

	/**
	 * @probed_modes: 探测阶段发现的原始模式链表（过滤前）。
	 *
	 * 这是从 DDC（Display Data Channel，I2C 读 EDID）或 BIOS 中获取的
	 * 原始模式列表，尚未经过 drm_helper_probe_single_connector_modes()
	 * 的有效性过滤（interlace_allowed、stereo_allowed 等）。
	 * 受 drm_mode_config.mutex 保护。
	 */
	struct list_head probed_modes;

	/**
	 * @display_info: 显示器的物理特性信息（drm_display_info）。
	 *
	 * 热插拔显示器：从 EDID 自动填充，包含：
	 *   - width_mm / height_mm：物理尺寸（毫米）
	 *   - bpc：每通道位深（bits per channel）
	 *   - color_formats：支持的色彩格式
	 *   - HDMI/DP 特有的能力位
	 *
	 * 非热插拔显示器（如嵌入式 LCD）：驱动应手动初始化 width_mm / height_mm。
	 * 受 drm_mode_config.mutex 保护。
	 */
	struct drm_display_info display_info;

	/**
	 * @funcs: 连接器操作函数指针集（drm_connector_funcs）。
	 *
	 * 核心回调：
	 *   - detect()：检测是否有显示器接入（读取 HPD 引脚状态）
	 *   - fill_modes()：枚举支持的显示模式
	 *     调用链：fill_modes() → helper.get_modes() → 读 EDID → 解析时序
	 *     → 填充 @modes 链表 → 用户空间据此选择分辨率 → 配置上游 CRTC
	 *   - destroy()：释放资源
	 *   - atomic_duplicate_state() / atomic_destroy_state()：原子状态管理
	 */
	const struct drm_connector_funcs *funcs;

	/**
	 * @edid_blob_ptr: 存储 EDID 二进制数据的 DRM blob 属性。
	 *
	 * EDID（Extended Display Identification Data）是显示器的"电子护照"，
	 * 128 或 256 字节，通过 DDC（I2C）读取，包含：
	 *   - 制造商、型号、序列号
	 *   - 支持的分辨率和刷新率
	 *   - 标准时序参数（像素时钟、HSYNC/VSYNC 宽度、消隐期等）
	 *   - 物理尺寸、色彩特性（色域、Gamma、HDR 能力）
	 *   - 音频能力（HDMI/DP 的 ELD 数据来源于此）
	 *   - 高级特性：VRR 支持范围、HDCP 版本、SCDC 能力等
	 *
	 * EDID 是配置信息**反向流动**的起点：
	 *   Connector 读 EDID → 驱动解析出时序参数 → 配置 CRTC 的像素时钟
	 *   → Encoder 据此生成对应频率的串行时钟 → Panel 的 TCON 锁定该时钟
	 *
	 * 所以 CRTC 的时序从来不是凭空设定的，而是严格遵循此处 EDID 里
	 * 屏幕声称能稳定支持的参数。
	 *
	 * 只能通过 drm_connector_update_edid_property() 更新，不要直接赋值。
	 * 受 drm_mode_config.mutex 保护。
	 */
	struct drm_property_blob *edid_blob_ptr;

	/**
	 * @properties: 此连接器挂载的所有 DRM 属性（drm_object_properties）。
	 *
	 * 属性是用户空间配置连接器行为的接口，例如：
	 *   - "EDID"：只读，暴露 EDID blob
	 *   - "DPMS"：读写，控制电源状态
	 *   - "scaling mode"：缩放模式（Full/Center/Aspect）
	 *   - "Content Protection"：HDCP 保护状态
	 *   - "Colorspace"：色彩空间选择
	 * 通过 drm_object_property_set_value() 设置，ioctl 查询/修改。
	 */
	struct drm_object_properties properties;

	/**
	 * @scaling_mode_property: 控制图像放大/缩放方式的原子属性（可选）。
	 *
	 * 枚举值（DRM_MODE_SCALE_*）：
	 *   - None：不缩放，像素对像素
	 *   - Full：拉伸到全屏（可能变形）
	 *   - Center：居中显示，黑边填充
	 *   - Full aspect：保持宽高比放大，黑边填充
	 *
	 * 常见于内嵌面板（eDP/LVDS），当内容分辨率低于面板原生分辨率时使用。
	 */
	struct drm_property *scaling_mode_property;

	/**
	 * @content_protection_property: HDCP 内容保护状态的 DRM 枚举属性。
	 *
	 * HDCP（High-bandwidth Digital Content Protection）防止数字内容被截取复制。
	 * 枚举值：
	 *   UNDESIRED  → 不启用 HDCP
	 *   DESIRED    → 希望启用 HDCP（驱动尽力而为）
	 *   ENABLED    → HDCP 已成功建立加密通道
	 *
	 * 通过 drm_connector_attach_content_protection_property() 注册。
	 */
	struct drm_property *content_protection_property;

	/**
	 * @colorspace_property: 色彩空间选择属性。
	 *
	 * 告知显示器（sink）当前输出的色彩空间，使其正确解码颜色。
	 * 常用值：sRGB、BT.709（HD）、BT.2020（HDR/UHD）、DCI-P3（专业显示）。
	 * HDMI 2.0+ 通过 AVI InfoFrame 传递此信息给显示器。
	 */
	struct drm_property *colorspace_property;

	/**
	 * @path_blob_ptr: DP MST 路径属性的 blob 数据。
	 *
	 * DisplayPort MST（Multi-Stream Transport）允许菊花链连接多台显示器。
	 * 路径字符串描述了信号从 GPU 到此连接器经过的 MST hub 路由，
	 * 例如 "mst:0-1-2" 表示经过第 1 个 hub 的第 2 个端口。
	 * 只能通过 drm_connector_set_path_property() 更新。
	 */
	struct drm_property_blob *path_blob_ptr;

/*
 * 连接检测（热插拔）轮询模式标志位：
 *
 * DRM_CONNECTOR_POLL_HPD (bit 0)：
 *   硬件支持 HPD（Hot Plug Detect）引脚中断，不需要软件轮询。
 *   插入/拔出时硬件自动触发中断，内核收到中断后更新状态。
 *   HDMI、DP 等接口通常使用此模式。
 *   注意：HPD 与 CONNECT/DISCONNECT 不能同时设置。
 *
 * DRM_CONNECTOR_POLL_CONNECT (bit 1)：
 *   定期轮询是否有新设备接入。
 *   适用于无 HPD 引脚的接口（如某些 VGA 实现）。
 *
 * DRM_CONNECTOR_POLL_DISCONNECT (bit 2)：
 *   定期轮询设备是否断开。
 *   对于 DAC（VGA）类接口要谨慎使用，轮询时发出的检测信号可能导致闪烁。
 * 
 * 注意：设为 0 表示此连接器不支持连接状态检测。
 */
#define DRM_CONNECTOR_POLL_HPD (1 << 0)
#define DRM_CONNECTOR_POLL_CONNECT (1 << 1)
#define DRM_CONNECTOR_POLL_DISCONNECT (1 << 2)

	/**
	 * @polled: 连接检测模式，由上面三个标志位组合而成。
	 *
	 * 典型配置：
	 *   HDMI/DP 接口：polled = DRM_CONNECTOR_POLL_HPD
	 *     → HPD 引脚电平跳变触发 GPIO 中断 → 内核唤醒 DRM 热插拔守护线程
	 *     → detect() 确认连接 → DDC 读 EDID 核验身份 → 资源分配 → 链路通车
	 *   无 HPD 的 VGA：polled = DRM_CONNECTOR_POLL_CONNECT
	 *     → 内核定时器周期性调用 detect() 探测
	 *   内嵌 DSI/eDP：polled = 0
	 *     → 面板通过 @panel 指针直接关联，无需运行时检测
	 */
	uint8_t polled;

	/**
	 * @dpms: 当前 DPMS（Display Power Management Signaling）电源状态。
	 *
	 * 四个标准状态（DRM_MODE_DPMS_*）：
	 *   ON       → 正常显示
	 *   STANDBY  → 待机（水平同步停止）
	 *   SUSPEND  → 挂起（垂直同步停止）
	 *   OFF      → 完全关闭
	 *
	 * 非原子驱动：必须在 drm_connector_funcs.dpms() 回调中手动更新此字段。
	 * 原子驱动：由内核原子框架自动管理，驱动只需关注 drm_crtc_state.active。
	 */
	int dpms;

	/**
	 * @helper_private: 连接器辅助层函数指针集（drm_connector_helper_funcs）。
	 *
	 * 提供更高级的操作接口：
	 *   - get_modes()：填充支持的显示模式（通常读取 EDID）
	 *   - mode_valid()：验证某个模式是否可用
	 *   - best_encoder()：为当前配置选择最佳 encoder
	 *   - atomic_check()：原子提交前的合法性检查
	 *
	 * 通过 drm_connector_helper_add() 设置。
	 */
	const struct drm_connector_helper_funcs *helper_private;

	/**
	 * @cmdline_mode: 从内核命令行参数解析出的强制显示模式。
	 *
	 * 通过内核参数 video=<connector>:<mode> 指定，例如：
	 *   video=HDMI-A-1:1920x1080@60
	 * 优先于 EDID 中的模式，用于调试或无法读取 EDID 的场景。
	 */
	struct drm_cmdline_mode cmdline_mode;

	/**
	 * @force: 强制连接状态（DRM_FORCE_<foo>）。
	 *
	 * 调试用途，通过 debugfs 或内核参数强制设置连接器状态：
	 *   DRM_FORCE_UNSPECIFIED → 正常检测
	 *   DRM_FORCE_OFF         → 强制断开
	 *   DRM_FORCE_ON          → 强制连接（即使没有物理设备）
	 *   DRM_FORCE_ON_DIGITAL  → 强制为数字连接模式
	 */
	enum drm_connector_force force;

	/**
	 * @override_edid: EDID 是否已通过 debugfs 被手动覆盖。
	 *
	 * 调试时可以通过 /sys/kernel/debug/dri/X/edid_override 写入假 EDID，
	 * 此标志为 true 时驱动跳过硬件 EDID 读取，使用覆盖的数据。
	 */
	bool override_edid;

/*
 * DRM_CONNECTOR_MAX_ENCODER：一个连接器最多可关联的 encoder 数量。
 * 值为 3，因为一个物理接口极少需要超过 3 个备选信号路径。
 */
#define DRM_CONNECTOR_MAX_ENCODER 3

	/**
	 * @encoder_ids: 此连接器可以关联的备选 encoder ID 列表。
	 *
	 * 存储最多 3 个 drm_encoder 的对象 ID（非索引，是用户空间可见的 ID）。
	 * 表示这些 encoder 在硬件上有能力驱动此连接器。
	 *
	 * 为什么一个 connector 可以有多个备选 encoder？
	 * 因为某些物理接口可以接收不同协议的信号。例如 DVI-I 接口
	 * 既能接收 TMDS 数字信号（对应一个 TMDS encoder），
	 * 也能接收 DAC 模拟信号（对应一个 DAC encoder）。
	 * DRM 驱动在 modeset 时会选择最合适的那个。
	 *
	 * 请只通过 drm_connector_for_each_possible_encoder() 宏遍历，
	 * 不要直接访问数组（因为未使用的槽位为 0）。
	 */
	uint32_t encoder_ids[DRM_CONNECTOR_MAX_ENCODER];

	/**
	 * @encoder: 当前正在驱动此连接器的 encoder 指针。
	 *
	 * 这是显示管道的**上游入口**——信号从这个 encoder 流入 connector。
	 * encoder 负责把 CRTC 的并行像素流编码为此 connector 接口能传输的格式。
	 *
	 * 如果 encoder 和 connector 之间存在 bridge，信号路径为：
	 *   encoder → encoder->bridge → [bridge chain] → connector
	 * bridge 链通过 encoder->bridge 指针串联，对 connector 透明。
	 *
	 * 非原子驱动：此字段有意义，表示当前绑定的 encoder。
	 * 原子驱动：此字段意义不大，应通过以下方式获取：
	 *   - drm_connector_state.best_encoder：当前选中的 encoder
	 *   - drm_connector_state.crtc：当前驱动的 CRTC
	 */
	struct drm_encoder *encoder;

	/**
	 * @loader_protect: Bootloader logo 保护状态（Rockchip 特有）。
	 *
	 * 为 true 时表示此连接器正在显示 bootloader 传递的 logo，
	 * 驱动应保持当前状态不变，避免初始化过程中出现黑屏，
	 * 实现从 bootloader 到内核的"无缝显示"过渡。
	 */
	bool loader_protect;

/*
 * MAX_ELD_BYTES：ELD 数据的最大字节数（128 字节）。
 * ELD = EDID-Like Data，是 EDID 的精简版，专门用于音频信息传递。
 */
#define MAX_ELD_BYTES	128

	/**
	 * @eld: ELD（EDID-Like Data）音频能力数据缓冲区。
	 *
	 * ELD 是从 EDID 中提取的音频相关信息的压缩版本，由显卡驱动填充后
	 * 传递给音频驱动，用于配置 HDMI/DP 音频输出格式。
	 * 包含：支持的音频编码格式、采样率、声道数、扬声器布局等。
	 * 通过 drm_eld_*() 系列函数访问。
	 */
	uint8_t eld[MAX_ELD_BYTES];

	/**
	 * @latency_present: ELD 中是否包含音视频延迟信息。
	 * [0] = 逐行内容的延迟数据是否存在
	 * [1] = 隔行内容的延迟数据是否存在
	 *
	 * 某些 HDMI 接收设备（AV 放大器、电视）需要此信息做音视频同步补偿。
	 */
	bool latency_present[2];

	/**
	 * @video_latency: 视频延迟（毫秒），从 ELD 中读取。
	 * [0] = 逐行内容的视频延迟
	 * [1] = 隔行内容的视频延迟
	 *
	 * 表示显示设备从接收信号到实际显示画面的处理延迟，
	 * 用于音视频同步（AV sync）补偿计算。
	 */
	int video_latency[2];

	/**
	 * @audio_latency: 音频延迟（毫秒），从 ELD 中读取。
	 * [0] = 逐行内容的音频延迟
	 * [1] = 隔行内容的音频延迟
	 *
	 * 与 video_latency 配合使用，确保声音和画面同步播放。
	 */
	int audio_latency[2];

	/**
	 * @null_edid_counter: 读取到全零 EDID 的次数计数。
	 *
	 * 部分硬件存在 bug，DDC 读取返回全 0x00 而非真实 EDID。
	 * 此计数器用于识别这类有问题的显示器，并触发相应的兼容性处理。
	 */
	int null_edid_counter;

	/**
	 * @bad_edid_counter: 读取到校验和错误 EDID 的次数计数。
	 *
	 * EDID 第 127 字节是前 127 字节的校验和，如果不匹配说明数据损坏。
	 * 用于统计不良显示器，并在内核日志中警告。
	 */
	unsigned bad_edid_counter;

	/*
	 * 以下字段来自 EDID 的 VCDB（Video Capability Data Block）和 HDR 信息块，
	 * 描述显示器的视频处理能力。
	 *
	 * @pt_scan_info: PT（preferred video format，首选视频格式）扫描信息，来自 VCDB。
	 *   注意：PT 不是 "Preferred Timing"（那是 EDID base block 中的概念），
	 *   而是针对显示器首选视频格式（preferred video format）的过扫/欠扫声明。
	 * @it_scan_info: IT（Information Technology，信息技术）内容扫描信息，来自 VCDB。
	 *   针对 PC 内容（桌面、游戏）的过扫/欠扫能力声明。
	 * @ce_scan_info: CE（Consumer Electronics，消费电子）内容扫描信息，来自 VCDB。
	 *   针对视频内容（电影、电视）的过扫/欠扫能力声明。
	 *
	 * 以上三个字段各占 2 bit，值的含义（见 CEA-861 标准）：
	 *   00 = 无数据（不声明）
	 *   01 = 始终过扫（overscan，图像超出可见区域，边缘被裁剪）
	 *   10 = 始终欠扫（underscan，图像完整显示，四周有黑边）
	 *   11 = 两者皆支持，可在过扫/欠扫之间切换
	 *
	 * 由 drm_extract_vcdb_info() 解析 EDID VCDB 数据块填入。
	 * @color_enc_fmt: 显示器支持的色彩编码格式位掩码（RGB/YCbCr 4:4:4/4:2:2/4:2:0）。
	 * @hdr_eotf: HDR 电光转换函数（Electro-Optical Transfer Function），来自 HDR 信息块。
	 *   指示显示器支持的 HDR 标准（SDR/HDR10/HLG 等）。
	 * @hdr_metadata_type_one: 是否支持 SMPTE ST 2086 HDR 静态元数据（HDR10 标准）。
	 * @hdr_max_luminance: 显示器支持的最大峰值亮度（nits）。
	 * @hdr_avg_luminance: 显示器支持的最大帧平均亮度（nits）。
	 * @hdr_min_luminance: 显示器支持的最小黑色亮度（nits），值越小对比度越好。
	 * @hdr_supported: 显示器是否支持 HDR 内容。
	 * @hdr_plus_app_ver: HDR10+ 应用版本号（动态 HDR 元数据）。
	 */
	u8 pt_scan_info;
	u8 it_scan_info;
	u8 ce_scan_info;
	u32 color_enc_fmt;
	u32 hdr_eotf;
	bool hdr_metadata_type_one;
	u32 hdr_max_luminance;
	u32 hdr_avg_luminance;
	u32 hdr_min_luminance;
	bool hdr_supported;
	u8 hdr_plus_app_ver;

	/*
	 * 以下字段来自 HDMI 2.0 EDID 扩展块，描述 HDMI 2.0 特有能力。
	 *
	 * @max_tmds_char: 最大 TMDS 字符率（Mcsc，兆字符/秒）。
	 *   决定最大带宽，600Mcsc 支持 4K@60Hz 18Gbps。
	 * @scdc_present: 是否支持 SCDC（Status and Control Data Channel）。
	 *   SCDC 是 HDMI 2.0 的控制通道，用于协商 TMDS 时钟比率、scrambling 等。
	 * @rr_capable: 是否支持 SCDC Read Request（接收端主动发起读请求）。
	 * @supports_scramble: 是否支持低于 340Mcsc 的 scrambling 模式。
	 *   Scrambling 用于降低 HDMI 信号的电磁干扰。
	 * @flags_3d: 支持的 3D 显示格式位掩码，见 drm_edid.h 中 DRM_EDID_3D_* 定义。
	 */
	int max_tmds_char;	/* 单位：Mcsc（兆字符/秒） */
	bool scdc_present;
	bool rr_capable;
	bool supports_scramble;
	int flags_3d;

	/**
	 * @edid_corrupt: 最近一次读取的 EDID 是否损坏。
	 *
	 * 用于 DisplayPort 合规性测试（DisplayPort Link CTS Core 1.2 rev1.1 4.2.2.6）。
	 * 当 EDID 校验失败或数据非法时置为 true，驱动可据此决定是否使用备用配置。
	 */
	bool edid_corrupt;

	/**
	 * @debugfs_entry: 此连接器在 debugfs 中的目录节点。
	 *
	 * 路径通常为 /sys/kernel/debug/dri/<minor>/connector-<name>/。
	 * 提供运行时调试接口，包括：
	 *   - edid：查看/覆盖 EDID 数据
	 *   - status：强制连接状态
	 *   - i2c：直接访问 DDC I2C 总线
	 */
	struct dentry *debugfs_entry;

	/**
	 * @state: 此连接器当前的原子状态（drm_connector_state）。
	 *
	 * 包含：best_encoder（选中的 encoder）、crtc（绑定的 CRTC）、
	 * 色彩空间、HDR 元数据、content protection 状态等。
	 *
	 * 受 drm_mode_config.connection_mutex 保护。
	 *
	 * 注意：非阻塞原子提交（nonblocking commit）在不持锁的情况下访问此字段，
	 * 通过以下方式安全访问：
	 *   - for_each_oldnew_connector_in_state()：同时获取旧/新状态快照
	 *   - for_each_old/new_connector_in_state()：单独获取旧/新状态
	 * 或依赖 drm_crtc_commit 的有序提交保证（原子辅助层实现）。
	 */
	struct drm_connector_state *state;

	/* DisplayID 拼接显示相关字段（来自 DisplayID extension block） */

	/**
	 * @tile_blob_ptr: 拼接（Tile）显示属性的 blob 数据。
	 *
	 * 用于多块屏幕拼接成一个逻辑大屏的场景（主要是 DP MST 多屏拼接）。
	 *
	 * 两种拼接类型：
	 *   - 非同步拼接（DP MST）：每块屏幕由独立 CRTC 驱动，时钟可能不同步，
	 *     需要 tile 属性让用户空间合成器知道各屏幕的位置关系。
	 *   - 同步拼接（双链路 LVDS/DSI）：两路信号 genlocked（时钟同步），
	 *     驱动应将其虚拟化为单个 CRTC 和 Plane，不暴露 tile 属性。
	 *
	 * 只能通过 drm_connector_set_tile_property() 更新。
	 */
	struct drm_property_blob *tile_blob_ptr;

	/**
	 * @has_tile: 此连接器是否连接到拼接显示器的一个分块。
	 * true 表示此连接器是大屏拼接中的一块，需要配合 tile_group 使用。
	 */
	bool has_tile;

	/**
	 * @tile_group: 此连接器所属的拼接组（drm_tile_group）。
	 *
	 * 同一个逻辑大屏的所有分块 connector 共享同一个 tile_group，
	 * 通过 tile_group->id 识别同组成员。
	 */
	struct drm_tile_group *tile_group;

	/**
	 * @tile_is_single_monitor: 此拼接是否物理上封装在同一个显示器外壳中。
	 *
	 * true：多路输入但单一外壳（如某些超宽屏通过双 DP 连接）
	 * false：独立的多台显示器拼接
	 */
	bool tile_is_single_monitor;

	/**
	 * @num_h_tile / @num_v_tile: 拼接组的水平/垂直分块总数。
	 *
	 * 例如 2x2 拼接大屏：num_h_tile=2, num_v_tile=2，共 4 块 connector。
	 */
	uint8_t num_h_tile, num_v_tile;

	/**
	 * @tile_h_loc / @tile_v_loc: 此分块在拼接布局中的位置（从 0 开始）。
	 *
	 * 例如 2x2 布局中左上角：tile_h_loc=0, tile_v_loc=0
	 *                   右下角：tile_h_loc=1, tile_v_loc=1
	 */
	uint8_t tile_h_loc, tile_v_loc;

	/**
	 * @tile_h_size / @tile_v_size: 此分块的像素尺寸（宽/高）。
	 *
	 * 拼接大屏的单块分辨率，用户空间据此计算整体布局。
	 * 例如 4K 大屏由两块 1920x2160 拼接：tile_h_size=1920, tile_v_size=2160。
	 */
	uint16_t tile_h_size, tile_v_size;

	/**
	 * @free_node: 无锁链表节点，用于安全地异步释放 connector。
	 *
	 * 仅供 drm_connector_list_iter 内部使用。
	 * 当 connector 被注销（热拔）后，不能立即释放（可能有其他上下文正在引用），
	 * 先将其挂入 drm_mode_config.connector_free_work 的工作队列，
	 * 由工作队列在安全时机执行实际释放。
	 */
	struct llist_node free_node;

	/**
	 * @hdr_sink_metadata: HDR 显示器的静态元数据（hdr_sink_metadata）。
	 *
	 * 从 EDID 的 HDR Static Metadata Data Block 解析，包含：
	 *   - 支持的 EOTF（HDR 传输函数）
	 *   - 静态元数据描述符类型
	 *   - 亮度范围（最大/最小/平均）
	 * 用于 HDR 内容的色调映射（tone mapping）。
	 */
	struct hdr_sink_metadata hdr_sink_metadata;

	/**
	 * @panel: 与此连接器关联的 drm_panel 面板对象（下游终端）。
	 *
	 * drm_panel 是面板驱动的抽象层，封装了面板的上电/下电/初始化序列。
	 * 这是显示管道的**最终下游**——信号穿过 connector 后，到达 panel 发光。
	 *
	 * 两类下游的不同处理方式：
	 *
	 * 内嵌面板（DSI/eDP/LVDS）：
	 *   - @panel 非 NULL，connector 通过此指针控制面板生命周期
	 *   - 上电序列：prepare() → 等待面板就绪 → enable() → 背光亮起
	 *   - 下电序列：disable() → 背光灭 → unprepare() → 断电
	 *   - EDID 可能由面板驱动硬编码提供（某些面板没有 EDID ROM）
	 *   - 信号路径：Encoder → [Bridge] → Connector → Panel（通过排线直连）
	 *
	 * 外部显示器（HDMI/DP）：
	 *   - @panel 为 NULL，显示器自行管理上电和初始化
	 *   - EDID 通过 DDC 通道（I2C）从显示器的 ROM 里读取
	 *   - 显示器的 TCON 芯片自行完成链路训练、时钟恢复
	 *   - 信号路径：Encoder → Connector → HDMI/DP 线缆 → 显示器（独立设备）
	 */
	struct drm_panel *panel;

	/**
	 * @checksum: 关联 EDID 前 127 字节的校验和计算值。
	 *
	 * EDID 标准规定第 128 字节是前 127 字节的二补数校验和（所有字节之和为 0）。
	 * 这里缓存计算得到的校验值，用于快速验证 EDID 完整性，
	 * 避免每次验证都重新计算。
	 */
	u8 checksum;
};

#define obj_to_connector(x) container_of(x, struct drm_connector, base)

int drm_connector_init(struct drm_device *dev,
		       struct drm_connector *connector,
		       const struct drm_connector_funcs *funcs,
		       int connector_type);
void drm_connector_attach_edid_property(struct drm_connector *connector);
int drm_connector_register(struct drm_connector *connector);
void drm_connector_unregister(struct drm_connector *connector);
int drm_connector_attach_encoder(struct drm_connector *connector,
				      struct drm_encoder *encoder);

void drm_connector_cleanup(struct drm_connector *connector);

static inline unsigned int drm_connector_index(const struct drm_connector *connector)
{
	return connector->index;
}

static inline u32 drm_connector_mask(const struct drm_connector *connector)
{
	return 1 << connector->index;
}

/**
 * drm_connector_lookup - lookup connector object
 * @dev: DRM device
 * @file_priv: drm file to check for lease against.
 * @id: connector object id
 *
 * This function looks up the connector object specified by id
 * add takes a reference to it.
 */
static inline struct drm_connector *drm_connector_lookup(struct drm_device *dev,
		struct drm_file *file_priv,
		uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, file_priv, id, DRM_MODE_OBJECT_CONNECTOR);
	return mo ? obj_to_connector(mo) : NULL;
}

/**
 * drm_connector_get - acquire a connector reference
 * @connector: DRM connector
 *
 * This function increments the connector's refcount.
 */
static inline void drm_connector_get(struct drm_connector *connector)
{
	drm_mode_object_get(&connector->base);
}

/**
 * drm_connector_put - release a connector reference
 * @connector: DRM connector
 *
 * This function decrements the connector's reference count and frees the
 * object if the reference count drops to zero.
 */
static inline void drm_connector_put(struct drm_connector *connector)
{
	drm_mode_object_put(&connector->base);
}

/**
 * drm_connector_reference - acquire a connector reference
 * @connector: DRM connector
 *
 * This is a compatibility alias for drm_connector_get() and should not be
 * used by new code.
 */
static inline void drm_connector_reference(struct drm_connector *connector)
{
	drm_connector_get(connector);
}

/**
 * drm_connector_unreference - release a connector reference
 * @connector: DRM connector
 *
 * This is a compatibility alias for drm_connector_put() and should not be
 * used by new code.
 */
static inline void drm_connector_unreference(struct drm_connector *connector)
{
	drm_connector_put(connector);
}

/**
 * drm_connector_is_unregistered - has the connector been unregistered from
 * userspace?
 * @connector: DRM connector
 *
 * Checks whether or not @connector has been unregistered from userspace.
 *
 * Returns:
 * True if the connector was unregistered, false if the connector is
 * registered or has not yet been registered with userspace.
 */
static inline bool
drm_connector_is_unregistered(struct drm_connector *connector)
{
	return READ_ONCE(connector->registration_state) ==
		DRM_CONNECTOR_UNREGISTERED;
}

const char *drm_get_connector_status_name(enum drm_connector_status status);
const char *drm_get_subpixel_order_name(enum subpixel_order order);
const char *drm_get_dpms_name(int val);
const char *drm_get_dvi_i_subconnector_name(int val);
const char *drm_get_dvi_i_select_name(int val);
const char *drm_get_tv_subconnector_name(int val);
const char *drm_get_tv_select_name(int val);
const char *drm_get_content_protection_name(int val);
const char *drm_get_connector_name(int val);

int drm_mode_create_dvi_i_properties(struct drm_device *dev);
int drm_mode_create_tv_properties(struct drm_device *dev,
				  unsigned int num_modes,
				  const char * const modes[]);
int drm_mode_create_scaling_mode_property(struct drm_device *dev);
int drm_connector_attach_content_type_property(struct drm_connector *dev);
int drm_connector_attach_scaling_mode_property(struct drm_connector *connector,
					       u32 scaling_mode_mask);
int drm_connector_attach_content_protection_property(
		struct drm_connector *connector);
int drm_mode_create_aspect_ratio_property(struct drm_device *dev);
int drm_mode_create_colorspace_property(struct drm_connector *connector);
int drm_mode_create_content_type_property(struct drm_device *dev);
void drm_hdmi_avi_infoframe_content_type(struct hdmi_avi_infoframe *frame,
					 const struct drm_connector_state *conn_state);

int drm_mode_create_suggested_offset_properties(struct drm_device *dev);

int drm_connector_set_path_property(struct drm_connector *connector,
				    const char *path);
int drm_connector_set_tile_property(struct drm_connector *connector);
int drm_connector_update_edid_property(struct drm_connector *connector,
				       const struct edid *edid);
void drm_connector_set_link_status_property(struct drm_connector *connector,
					    uint64_t link_status);
int drm_connector_init_panel_orientation_property(
	struct drm_connector *connector, int width, int height);

/**
 * struct drm_tile_group - Tile group metadata
 * @refcount: reference count
 * @dev: DRM device
 * @id: tile group id exposed to userspace
 * @group_data: Sink-private data identifying this group
 *
 * @group_data corresponds to displayid vend/prod/serial for external screens
 * with an EDID.
 */
struct drm_tile_group {
	struct kref refcount;
	struct drm_device *dev;
	int id;
	u8 group_data[8];
};

struct drm_tile_group *drm_mode_create_tile_group(struct drm_device *dev,
						  char topology[8]);
struct drm_tile_group *drm_mode_get_tile_group(struct drm_device *dev,
					       char topology[8]);
void drm_mode_put_tile_group(struct drm_device *dev,
			     struct drm_tile_group *tg);

/**
 * struct drm_connector_list_iter - connector_list iterator
 *
 * This iterator tracks state needed to be able to walk the connector_list
 * within struct drm_mode_config. Only use together with
 * drm_connector_list_iter_begin(), drm_connector_list_iter_end() and
 * drm_connector_list_iter_next() respectively the convenience macro
 * drm_for_each_connector_iter().
 */
struct drm_connector_list_iter {
/* private: */
	struct drm_device *dev;
	struct drm_connector *conn;
};

void drm_connector_list_iter_begin(struct drm_device *dev,
				   struct drm_connector_list_iter *iter);
struct drm_connector *
drm_connector_list_iter_next(struct drm_connector_list_iter *iter);
void drm_connector_list_iter_end(struct drm_connector_list_iter *iter);

bool drm_connector_has_possible_encoder(struct drm_connector *connector,
					struct drm_encoder *encoder);

/**
 * drm_for_each_connector_iter - connector_list iterator macro
 * @connector: &struct drm_connector pointer used as cursor
 * @iter: &struct drm_connector_list_iter
 *
 * Note that @connector is only valid within the list body, if you want to use
 * @connector after calling drm_connector_list_iter_end() then you need to grab
 * your own reference first using drm_connector_get().
 */
#define drm_for_each_connector_iter(connector, iter) \
	while ((connector = drm_connector_list_iter_next(iter)))

/**
 * drm_connector_for_each_possible_encoder - iterate connector's possible encoders
 * @connector: &struct drm_connector pointer
 * @encoder: &struct drm_encoder pointer used as cursor
 * @__i: int iteration cursor, for macro-internal use
 */
#define drm_connector_for_each_possible_encoder(connector, encoder, __i) \
	for ((__i) = 0; (__i) < ARRAY_SIZE((connector)->encoder_ids) && \
		     (connector)->encoder_ids[(__i)] != 0; (__i)++) \
		for_each_if((encoder) = \
			    drm_encoder_find((connector)->dev, NULL, \
					     (connector)->encoder_ids[(__i)])) \

#endif
