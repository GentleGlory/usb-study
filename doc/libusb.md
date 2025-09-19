# 概述
* libusb為一個使用 C 寫的 lib 。可以透過它在 User space 來訪問 USB 設備。 不需要再編寫額外的驅動程式。
* 跨平台。 Linux 、 Windows 、Android 等等平台。
* 不需要 root 權限。
* 所有 USB 協議都支援。 1.0 ~ 3.1 。

# 用法
```mermaid
graph TD
    A0(開始) --> A1[libusb_init]

    subgraph 初始化
        A1
    end

    A1 --> B1[libusb_get_device_list]
    A1 --> B2[libusb_open_device_with_vid_pid]

    subgraph 打開設備
        B1 --> B1_1["libusb_get_device_descriptor <br>或<br> libusb_get_config_descriptor"]
        B1_1 --> B1_2[libusb_open]
        B1_2 --> B1_3[libusb_free_config_descriptor]
        B1_3 --> B1_4[libusb_free_device_list]
    end

    subgraph 移除原驅動/認領接口
        C1[libusb_set_auto_detach_kernel_driver] --> C2[libusb_detach_kernel_driver] --> C3[libusb_claim_interface]
    end

    B1_4 --> C1
    B2 --> C2

    subgraph 傳輸
        direction LR
        subgraph 同步傳輸
            D1["libusb_control_transfer <br>或<br> libusb_bulk_transfer <br>或<br> libusb_interrupt_transfer"]
        end
        subgraph 異步傳輸
            D2_1[libusb_alloc_transfer] --> D2_2["libusb_fill_control_transfer<br>libusb_fill_bulk_transfer<br>libusb_fill_interrupt_transfer<br>libusb_fill_iso_transfer"]
            D2_2 --> D2_3[libusb_submit_transfer]
            D2_3 --> D2_4["libusb_handle_events_timeout_completed<br>或<br>libusb_handle_events_timeout<br>或<br>libusb_handle_events_completed<br>或<br>libusb_handle_events"]
            D2_4 --> D2_5[libusb_free_transfer]
        end
    end

    C3 --> D1
    C3 --> D2_1

    subgraph 關閉設備
        E1[libusb_release_interface] --> E2[libusb_close]
    end

    D1 --> E1
    D2_5 --> E1
```
# Noted
* 在異步傳輸時。 程式要結束時，做 cleanup 動作可以對每個 transfer 呼叫 `libusb_cancel_transfer` 。程式會進到 call back function 中，且 transfer 的 status 不為 `LIBUSB_TRANSFER_COMPLETED` 。 此時可以做 error handling ，把 transfer free 掉即可安全的結束程式。 詳細流程可以參考 mouse_ctrl_test_async 。