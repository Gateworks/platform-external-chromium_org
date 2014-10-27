// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/webrtc/video_destination_handler.h"

#include <string>

#include "base/base64.h"
#include "base/debug/trace_event.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/renderer/media/media_stream.h"
#include "content/renderer/media/media_stream_registry_interface.h"
#include "content/renderer/media/media_stream_video_track.h"
#include "content/renderer/pepper/ppb_image_data_impl.h"
#include "content/renderer/render_thread_impl.h"
#include "media/video/capture/video_capture_types.h"
#include "third_party/WebKit/public/platform/WebMediaStreamTrack.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/web/WebMediaStreamRegistry.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "url/gurl.h"

namespace content {

class PpFrameWriter::FrameWriterDelegate
    : public base::RefCountedThreadSafe<FrameWriterDelegate> {
 public:
  FrameWriterDelegate(
      const scoped_refptr<base::MessageLoopProxy>& io_message_loop_proxy);

  // Starts forwarding frames to |frame_callback| on the  IO-thread that are
  // delivered to this class by calling DeliverFrame on the main render thread.
  void StartDeliver(const VideoCaptureDeliverFrameCB& frame_callback);
  void StopDeliver();

  void DeliverFrame(const scoped_refptr<PPB_ImageData_Impl>& image_data,
                    int64 time_stamp_ns);

 private:
  friend class base::RefCountedThreadSafe<FrameWriterDelegate>;
  // Endian in memory order, e.g. AXXX stands for uint8 pixel[4] = {A, x, x, x};
  enum PixelEndian {
    UNKNOWN,
    AXXX,
    XXXA,
  };

  virtual ~FrameWriterDelegate();

  void StartDeliverOnIO(const VideoCaptureDeliverFrameCB& frame_callback);
  void StopDeliverOnIO();

  void DeliverFrameOnIO(uint8* data, int stride, int width, int height,
                        int64 time_stamp_ns);
  void FrameDelivered(const scoped_refptr<PPB_ImageData_Impl>& image_data);

  scoped_refptr<base::MessageLoopProxy> io_message_loop_;

  // |frame_pool_|, |new_frame_callback_| and |endian_| are only used on the
  // IO-thread.
  media::VideoFramePool frame_pool_;
  VideoCaptureDeliverFrameCB new_frame_callback_;
  PixelEndian endian_;

  // Used to DCHECK that we are called on the main render thread.
  base::ThreadChecker thread_checker_;
};

PpFrameWriter::FrameWriterDelegate::FrameWriterDelegate(
    const scoped_refptr<base::MessageLoopProxy>& io_message_loop_proxy)
    : io_message_loop_(io_message_loop_proxy), endian_(UNKNOWN) {
}

PpFrameWriter::FrameWriterDelegate::~FrameWriterDelegate() {
}

void PpFrameWriter::FrameWriterDelegate::StartDeliver(
    const VideoCaptureDeliverFrameCB& frame_callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  io_message_loop_->PostTask(
      FROM_HERE,
      base::Bind(&FrameWriterDelegate::StartDeliverOnIO, this,
                 frame_callback));
}

void PpFrameWriter::FrameWriterDelegate::StopDeliver() {
  DCHECK(thread_checker_.CalledOnValidThread());
  io_message_loop_->PostTask(
      FROM_HERE,
      base::Bind(&FrameWriterDelegate::StopDeliverOnIO, this));
}

void PpFrameWriter::FrameWriterDelegate::DeliverFrame(
    const scoped_refptr<PPB_ImageData_Impl>& image_data,
    int64 time_stamp_ns) {
  DCHECK(thread_checker_.CalledOnValidThread());
  TRACE_EVENT0("video", "PpFrameWriter::FrameWriterDelegate::DeliverFrame");
  if (!image_data->Map()) {
    LOG(ERROR) << "PpFrameWriter::PutFrame - "
               << "The image could not be mapped and is unusable.";
    return;
  }

  const SkBitmap* bitmap = image_data->GetMappedBitmap();
  if (!bitmap) {
    LOG(ERROR) << "PpFrameWriter::PutFrame - "
               << "The image_data's mapped bitmap is NULL.";
    return;
  }
  // We only support PP_IMAGEDATAFORMAT_BGRA_PREMUL at the moment.
  DCHECK(image_data->format() == PP_IMAGEDATAFORMAT_BGRA_PREMUL);
  io_message_loop_->PostTaskAndReply(
      FROM_HERE,
      base::Bind(&FrameWriterDelegate::DeliverFrameOnIO, this,
                 static_cast<uint8*>(bitmap->getPixels()),
                 bitmap->rowBytes(),
                 bitmap->width(),
                 bitmap->height(),
                 time_stamp_ns),
      base::Bind(&FrameWriterDelegate::FrameDelivered, this,
                 image_data));
}

void PpFrameWriter::FrameWriterDelegate::FrameDelivered(
    const scoped_refptr<PPB_ImageData_Impl>& image_data) {
  DCHECK(thread_checker_.CalledOnValidThread());
  image_data->Unmap();
}

void PpFrameWriter::FrameWriterDelegate::StartDeliverOnIO(
    const VideoCaptureDeliverFrameCB& frame_callback) {
  DCHECK(io_message_loop_->BelongsToCurrentThread());
  new_frame_callback_ = frame_callback;
}
void PpFrameWriter::FrameWriterDelegate::StopDeliverOnIO() {
  DCHECK(io_message_loop_->BelongsToCurrentThread());
  new_frame_callback_.Reset();
}

void PpFrameWriter::FrameWriterDelegate::DeliverFrameOnIO(
    uint8* data, int stride, int width, int height, int64 time_stamp_ns) {
  DCHECK(io_message_loop_->BelongsToCurrentThread());
  TRACE_EVENT0("video", "PpFrameWriter::FrameWriterDelegate::DeliverFrameOnIO");

  if (new_frame_callback_.is_null())
    return;

  const gfx::Size frame_size(width, height);
  const base::TimeDelta timestamp = base::TimeDelta::FromMicroseconds(
      time_stamp_ns / base::Time::kNanosecondsPerMicrosecond);

  scoped_refptr<media::VideoFrame> new_frame =
      frame_pool_.CreateFrame(media::VideoFrame::YV12, frame_size,
                              gfx::Rect(frame_size), frame_size, timestamp);
  media::VideoCaptureFormat format(
      frame_size,
      MediaStreamVideoSource::kUnknownFrameRate,
      media::PIXEL_FORMAT_YV12);

  // TODO(magjed): Remove this and always use libyuv::ARGBToI420 when
  // crbug/426020 is fixed.
  // Due to a change in endianness, we try to determine it from the data.
  // The alpha channel is always 255. It is unlikely for other color channels to
  // be 255, so we will most likely break on the first few pixels in the first
  // frame.
  uint8* row_ptr = data;
  // Note that we only do this if endian_ is still UNKNOWN.
  for (int y = 0; y < height && endian_ == UNKNOWN; ++y) {
    for (int x = 0; x < width; ++x) {
      if (row_ptr[x * 4 + 0] != 255) {  // First byte is not Alpha => XXXA.
        endian_ = XXXA;
        break;
      }
      if (row_ptr[x * 4 + 3] != 255) {  // Fourth byte is not Alpha => AXXX.
        endian_ = AXXX;
        break;
      }
    }
    row_ptr += stride;
  }
  if (endian_ == UNKNOWN) {
    LOG(WARNING) << "PpFrameWriter::FrameWriterDelegate::DeliverFrameOnIO - "
                 << "Could not determine endianness.";
  }
  // libyuv specifies fourcc/channel ordering the same as webrtc. That is why
  // the naming is reversed compared to PixelEndian and PP_ImageDataFormat which
  // describes the memory layout from the lowest address to the highest.
  auto xxxxToI420 =
      (endian_ == AXXX) ? &libyuv::BGRAToI420 : &libyuv::ARGBToI420;
  xxxxToI420(data,
             stride,
             new_frame->data(media::VideoFrame::kYPlane),
             new_frame->stride(media::VideoFrame::kYPlane),
             new_frame->data(media::VideoFrame::kUPlane),
             new_frame->stride(media::VideoFrame::kUPlane),
             new_frame->data(media::VideoFrame::kVPlane),
             new_frame->stride(media::VideoFrame::kVPlane),
             width,
             height);

  // The local time when this frame is generated is unknown so give a null
  // value to |estimated_capture_time|.
  new_frame_callback_.Run(new_frame, format, base::TimeTicks());
}

PpFrameWriter::PpFrameWriter() {
  DVLOG(3) << "PpFrameWriter ctor";
  delegate_ = new FrameWriterDelegate(io_message_loop());
}

PpFrameWriter::~PpFrameWriter() {
  DVLOG(3) << "PpFrameWriter dtor";
}

VideoDestinationHandler::FrameWriterCallback
PpFrameWriter::GetFrameWriterCallback() {
  DCHECK(CalledOnValidThread());
  return base::Bind(&PpFrameWriter::FrameWriterDelegate::DeliverFrame,
                    delegate_);
}

void PpFrameWriter::GetCurrentSupportedFormats(
    int max_requested_width,
    int max_requested_height,
    double max_requested_frame_rate,
    const VideoCaptureDeviceFormatsCB& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(3) << "PpFrameWriter::GetCurrentSupportedFormats()";
  // Since the input is free to change the resolution at any point in time
  // the supported formats are unknown.
  media::VideoCaptureFormats formats;
  callback.Run(formats);
}

void PpFrameWriter::StartSourceImpl(
    const media::VideoCaptureFormat& format,
    const VideoCaptureDeliverFrameCB& frame_callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(3) << "PpFrameWriter::StartSourceImpl()";
  delegate_->StartDeliver(frame_callback);
  OnStartDone(MEDIA_DEVICE_OK);
}

void PpFrameWriter::StopSourceImpl() {
  DCHECK(CalledOnValidThread());
  delegate_->StopDeliver();
}

bool VideoDestinationHandler::Open(
    MediaStreamRegistryInterface* registry,
    const std::string& url,
    FrameWriterCallback* frame_writer) {
  DVLOG(3) << "VideoDestinationHandler::Open";
  blink::WebMediaStream stream;
  if (registry) {
    stream = registry->GetMediaStream(url);
  } else {
    stream =
        blink::WebMediaStreamRegistry::lookupMediaStreamDescriptor(GURL(url));
  }
  if (stream.isNull()) {
    LOG(ERROR) << "VideoDestinationHandler::Open - invalid url: " << url;
    return false;
  }

  // Create a new native video track and add it to |stream|.
  std::string track_id;
  // According to spec, a media stream source's id should be unique per
  // application. There's no easy way to strictly achieve that. The id
  // generated with this method should be unique for most of the cases but
  // theoretically it's possible we can get an id that's duplicated with the
  // existing sources.
  base::Base64Encode(base::RandBytesAsString(64), &track_id);

  PpFrameWriter* writer = new PpFrameWriter();
  *frame_writer = writer->GetFrameWriterCallback();

  // Create a new webkit video track.
  blink::WebMediaStreamSource webkit_source;
  blink::WebMediaStreamSource::Type type =
      blink::WebMediaStreamSource::TypeVideo;
  blink::WebString webkit_track_id = base::UTF8ToUTF16(track_id);
  webkit_source.initialize(webkit_track_id, type, webkit_track_id);
  webkit_source.setExtraData(writer);

  blink::WebMediaConstraints constraints;
  constraints.initialize();
  bool track_enabled = true;

  stream.addTrack(MediaStreamVideoTrack::CreateVideoTrack(
      writer, constraints, MediaStreamVideoSource::ConstraintsCallback(),
      track_enabled));

  return true;
}

}  // namespace content
