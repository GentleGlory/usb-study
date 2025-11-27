# 專案架構
- doc
	存放學習USB的筆記
- mouse_ctrl_test
	用 libusb 實作一個抓取滑鼠數據的小程式。
- mouse_ctrl_test_async
	用 libusb 實作一個抓取滑鼠數據的小程式，並使用非同步的方法去抓資料。
- mouse_as_key
	實作一個 USB 驅動。當使用者按下滑鼠按鍵時，會觸發鍵盤按鍵的事件。
- libusb_zero
	測試 zero gadget 的 sourcesink 和 loopback 兩個 function。

# 參考資料
1. [Linux驱动开发与使用大全](https://e.coding.net/weidongshan/linux/doc_and_source_for_drivers.git)

