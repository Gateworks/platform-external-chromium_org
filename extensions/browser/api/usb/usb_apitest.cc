// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_apitest.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_utils.h"
#include "device/usb/usb_service.h"
#include "extensions/browser/api/usb/usb_api.h"
#include "extensions/shell/test/shell_apitest.h"
#include "net/base/io_buffer.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::AnyNumber;
using testing::_;
using testing::Return;
using testing::ReturnRef;
using content::BrowserThread;
using device::UsbConfigDescriptor;
using device::UsbDevice;
using device::UsbDeviceHandle;
using device::UsbEndpointDirection;
using device::UsbInterfaceDescriptor;
using device::UsbService;
using device::UsbTransferCallback;

namespace extensions {

namespace {

ACTION_TEMPLATE(InvokeUsbTransferCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(p1)) {
  net::IOBuffer* io_buffer = new net::IOBuffer(1);
  memset(io_buffer->data(), 0, 1);  // Avoid uninitialized reads.
  ::std::tr1::get<k>(args).Run(p1, io_buffer, 1);
}

class MockUsbDeviceHandle : public UsbDeviceHandle {
 public:
  MockUsbDeviceHandle() : UsbDeviceHandle() {}

  MOCK_METHOD0(Close, void());

  MOCK_METHOD10(ControlTransfer,
                void(UsbEndpointDirection direction,
                     TransferRequestType request_type,
                     TransferRecipient recipient,
                     uint8 request,
                     uint16 value,
                     uint16 index,
                     net::IOBuffer* buffer,
                     size_t length,
                     unsigned int timeout,
                     const UsbTransferCallback& callback));

  MOCK_METHOD6(BulkTransfer,
               void(UsbEndpointDirection direction,
                    uint8 endpoint,
                    net::IOBuffer* buffer,
                    size_t length,
                    unsigned int timeout,
                    const UsbTransferCallback& callback));

  MOCK_METHOD6(InterruptTransfer,
               void(UsbEndpointDirection direction,
                    uint8 endpoint,
                    net::IOBuffer* buffer,
                    size_t length,
                    unsigned int timeout,
                    const UsbTransferCallback& callback));

  MOCK_METHOD8(IsochronousTransfer,
               void(UsbEndpointDirection direction,
                    uint8 endpoint,
                    net::IOBuffer* buffer,
                    size_t length,
                    unsigned int packets,
                    unsigned int packet_length,
                    unsigned int timeout,
                    const UsbTransferCallback& callback));

  MOCK_METHOD0(ResetDevice, bool());
  MOCK_METHOD2(GetStringDescriptor, bool(uint8_t, base::string16*));
  MOCK_METHOD1(ClaimInterface, bool(int interface_number));
  MOCK_METHOD1(ReleaseInterface, bool(int interface_number));
  MOCK_METHOD2(SetInterfaceAlternateSetting,
               bool(int interface_number, int alternate_setting));

  virtual scoped_refptr<UsbDevice> GetDevice() const override {
    return device_;
  }

  void set_device(UsbDevice* device) { device_ = device; }

 protected:
  UsbDevice* device_;

  virtual ~MockUsbDeviceHandle() {}
};

class MockUsbDevice : public UsbDevice {
 public:
  explicit MockUsbDevice(MockUsbDeviceHandle* mock_handle)
      : UsbDevice(0, 0, 0), mock_handle_(mock_handle) {
    mock_handle->set_device(this);
  }

  virtual scoped_refptr<UsbDeviceHandle> Open() override {
    return mock_handle_;
  }

  virtual bool Close(scoped_refptr<UsbDeviceHandle> handle) override {
    EXPECT_TRUE(false) << "Should not be reached";
    return false;
  }

#if defined(OS_CHROMEOS)
  virtual void RequestUsbAccess(
      int interface_id,
      const base::Callback<void(bool success)>& callback) override {
    BrowserThread::PostTask(
          BrowserThread::FILE, FROM_HERE, base::Bind(callback, true));
  }
#endif  // OS_CHROMEOS

  MOCK_METHOD0(GetConfiguration, const UsbConfigDescriptor&());
  MOCK_METHOD1(GetManufacturer, bool(base::string16* manufacturer));
  MOCK_METHOD1(GetProduct, bool(base::string16* product));
  MOCK_METHOD1(GetSerialNumber, bool(base::string16* serial_number));

 private:
  MockUsbDeviceHandle* mock_handle_;
  virtual ~MockUsbDevice() {}
};

class MockUsbService : public UsbService {
 public:
  explicit MockUsbService(scoped_refptr<UsbDevice> device) : device_(device) {}

 protected:
  scoped_refptr<UsbDevice> GetDeviceById(uint32 unique_id) override {
    EXPECT_EQ(unique_id, 0U);
    return device_;
  }

  void GetDevices(std::vector<scoped_refptr<UsbDevice>>* devices) override {
    STLClearObject(devices);
    devices->push_back(device_);
  }

  scoped_refptr<UsbDevice> device_;
};

class UsbApiTest : public ShellApiTest {
 public:
  void SetUpOnMainThread() override {
    ShellApiTest::SetUpOnMainThread();
    mock_device_handle_ = new MockUsbDeviceHandle();
    mock_device_ = new MockUsbDevice(mock_device_handle_.get());
    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    BrowserThread::PostTaskAndReply(BrowserThread::FILE,
                                    FROM_HERE,
                                    base::Bind(&UsbApiTest::SetUpService, this),
                                    runner->QuitClosure());
    runner->Run();
  }

  void SetUpService() {
    UsbService::SetInstanceForTest(new MockUsbService(mock_device_));
  }

  void TearDownOnMainThread() override {
    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    UsbService* service = NULL;
    BrowserThread::PostTaskAndReply(
        BrowserThread::FILE,
        FROM_HERE,
        base::Bind(&UsbService::SetInstanceForTest, service),
        runner->QuitClosure());
    runner->Run();
  }

 protected:
  scoped_refptr<MockUsbDeviceHandle> mock_device_handle_;
  scoped_refptr<MockUsbDevice> mock_device_;
};

}  // namespace

IN_PROC_BROWSER_TEST_F(UsbApiTest, DeviceHandling) {
  EXPECT_CALL(*mock_device_handle_.get(), Close()).Times(4);
  ASSERT_TRUE(RunAppTest("api_test/usb/device_handling"));
}

IN_PROC_BROWSER_TEST_F(UsbApiTest, ResetDevice) {
  EXPECT_CALL(*mock_device_handle_.get(), Close()).Times(2);
  EXPECT_CALL(*mock_device_handle_.get(), ResetDevice())
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_device_handle_.get(),
              InterruptTransfer(device::USB_DIRECTION_OUTBOUND, 2, _, 1, _, _))
      .WillOnce(InvokeUsbTransferCallback<5>(device::USB_TRANSFER_COMPLETED));
  ASSERT_TRUE(RunAppTest("api_test/usb/reset_device"));
}

IN_PROC_BROWSER_TEST_F(UsbApiTest, ListInterfaces) {
  UsbConfigDescriptor config_descriptor;
  EXPECT_CALL(*mock_device_handle_.get(), Close()).Times(AnyNumber());
  EXPECT_CALL(*mock_device_.get(), GetConfiguration())
      .WillOnce(ReturnRef(config_descriptor));
  ASSERT_TRUE(RunAppTest("api_test/usb/list_interfaces"));
}

IN_PROC_BROWSER_TEST_F(UsbApiTest, TransferEvent) {
  EXPECT_CALL(*mock_device_handle_.get(),
              ControlTransfer(device::USB_DIRECTION_OUTBOUND,
                              UsbDeviceHandle::STANDARD,
                              UsbDeviceHandle::DEVICE,
                              1,
                              2,
                              3,
                              _,
                              1,
                              _,
                              _))
      .WillOnce(InvokeUsbTransferCallback<9>(device::USB_TRANSFER_COMPLETED));
  EXPECT_CALL(*mock_device_handle_.get(),
              BulkTransfer(device::USB_DIRECTION_OUTBOUND, 1, _, 1, _, _))
      .WillOnce(InvokeUsbTransferCallback<5>(device::USB_TRANSFER_COMPLETED));
  EXPECT_CALL(*mock_device_handle_.get(),
              InterruptTransfer(device::USB_DIRECTION_OUTBOUND, 2, _, 1, _, _))
      .WillOnce(InvokeUsbTransferCallback<5>(device::USB_TRANSFER_COMPLETED));
  EXPECT_CALL(
      *mock_device_handle_.get(),
      IsochronousTransfer(device::USB_DIRECTION_OUTBOUND, 3, _, 1, 1, 1, _, _))
      .WillOnce(InvokeUsbTransferCallback<7>(device::USB_TRANSFER_COMPLETED));
  EXPECT_CALL(*mock_device_handle_.get(), Close()).Times(AnyNumber());
  ASSERT_TRUE(RunAppTest("api_test/usb/transfer_event"));
}

IN_PROC_BROWSER_TEST_F(UsbApiTest, ZeroLengthTransfer) {
  EXPECT_CALL(*mock_device_handle_.get(), BulkTransfer(_, _, _, 0, _, _))
      .WillOnce(InvokeUsbTransferCallback<5>(device::USB_TRANSFER_COMPLETED));
  EXPECT_CALL(*mock_device_handle_.get(), Close()).Times(AnyNumber());
  ASSERT_TRUE(RunAppTest("api_test/usb/zero_length_transfer"));
}

IN_PROC_BROWSER_TEST_F(UsbApiTest, TransferFailure) {
  EXPECT_CALL(*mock_device_handle_.get(), BulkTransfer(_, _, _, _, _, _))
      .WillOnce(InvokeUsbTransferCallback<5>(device::USB_TRANSFER_COMPLETED))
      .WillOnce(InvokeUsbTransferCallback<5>(device::USB_TRANSFER_ERROR))
      .WillOnce(InvokeUsbTransferCallback<5>(device::USB_TRANSFER_TIMEOUT));
  EXPECT_CALL(*mock_device_handle_.get(), Close()).Times(AnyNumber());
  ASSERT_TRUE(RunAppTest("api_test/usb/transfer_failure"));
}

IN_PROC_BROWSER_TEST_F(UsbApiTest, InvalidLengthTransfer) {
  EXPECT_CALL(*mock_device_handle_.get(), Close()).Times(AnyNumber());
  ASSERT_TRUE(RunAppTest("api_test/usb/invalid_length_transfer"));
}

}  // namespace extensions
