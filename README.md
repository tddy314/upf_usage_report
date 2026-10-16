# Demo phát hiện Usage Report chậm khi tải cao

Đây là **giả lập**, không phải UPF thật. Chương trình C dùng pthread trên
Linux hoặc WSL, không dùng DPDK, PFCP, socket, database hay thư viện ngoài.
“Gửi report” chỉ có nghĩa là hoàn thành xử lý giả lập, không gửi bản tin mạng.

## Build và chạy

Cần GCC (hoặc trình biên dịch C11 tương thích), Make và pthread của hệ thống.

```sh
make
./demo normal
./demo overload
```

Mỗi lần chạy tạo 30 report, ID từ 1 đến 30. Report đầu tiên được tạo ngay.
Không có tham số CLI được hiểu là kịch bản mặc định: phải chọn một trong hai
tên trên; tham số không hợp lệ sẽ báo cách dùng và trả mã lỗi.

| Kịch bản | Khoảng tạo report | Thời gian xử lý giả lập | Ngưỡng SLOW |
| --- | --- | --- | --- |
| normal | 200 ms | 50 ms | 300 ms |
| overload | 50 ms | 200 ms | 300 ms |

Các giá trị cấu hình nằm trong phần khởi tạo `Simulation` ở `main.c`.
Dùng `make clean` để xóa executable được tạo ra.

## Mô hình

- **Producer** đại diện cho công đoạn phát sinh yêu cầu Usage Report: ghi ID,
  thời điểm tạo rồi đưa vào queue.
- **Queue vòng** đại diện cho các report đang chờ xử lý, sức chứa cố định 16
  report (không tính report consumer đang xử lý). Queue đầy thì bỏ report mới,
  in `DROPPED` và tăng bộ đếm `dropped`.
- **Consumer** đại diện cho công đoạn tạo/gửi report: lấy report, ghi thời điểm
  bắt đầu, ngủ để mô phỏng xử lý rồi ghi thời điểm hoàn thành.

Mutex bảo vệ queue và cờ kết thúc. Consumer dùng condition variable khi queue
rỗng, không busy-wait. Khi producer kết thúc, nó đặt cờ và đánh thức consumer.
Consumer xử lý hết queue rồi thoát; main chờ cả hai luồng bằng `pthread_join`.
Mutex không bị giữ trong thời gian ngủ giả lập xử lý.

## Đo độ trễ

Ba mốc `created_at`, `started_at`, `finished_at` đều dùng
`clock_gettime(CLOCK_MONOTONIC)` để không phụ thuộc chỉnh giờ hệ thống.

```text
queue_wait_ms = started_at - created_at
processing_ms = finished_at - started_at
total_ms = finished_at - created_at
```

Mỗi report xử lý xong được in với ID, `queue`, `processing`, `total` (ms)
và nhãn `OK` hoặc `SLOW`. Chỉ khi `total_ms > 300` mới là `SLOW`.
Mọi độ trễ in ra lấy từ phép đo thực tế; thời gian sleep có thể lệch do hệ điều
hành lập lịch. Các số được hiển thị đến ba chữ số thập phân.

Ở normal, tốc độ đến khoảng 5 report/giây thấp hơn khả năng xử lý khoảng
20 report/giây nên queue thường ít tích tụ. Ở overload, khoảng 20 report/giây
đến trong khi consumer chỉ xử lý khoảng 5 report/giây: queue tăng, thời gian
chờ và tổng độ trễ tăng, xuất hiện `SLOW`; queue đầy sẽ gây `DROPPED`.
Không đảm bảo số lượng dropped hay độ trễ cố định giữa các lần chạy.

`SLOW` là report **đã xử lý thành công nhưng chậm**; `DROPPED` là report
**bị bỏ vì queue đầy**, không được đưa vào thống kê độ trễ hay số SLOW.

## Tổng kết và kiểm tra

Cuối mỗi lần chạy, chương trình in số created, processed, dropped, SLOW;
trung bình/lớn nhất của total và queue_wait; độ dài queue lớn nhất.
Trung bình chỉ tính các report xử lý thành công. Sau khi join cả hai luồng,
chương trình kiểm tra `created = processed + dropped`, in PASS/FAIL và trả mã
lỗi nếu không khớp.

Có thể kiểm tra chương trình không treo bằng công cụ `timeout` trên Linux:

```sh
timeout 20s ./demo normal
timeout 20s ./demo overload
```

Kiểm tra cả hai lệnh trả mã 0, created bằng 30, phép kiểm tra bộ đếm PASS,
queue_max không vượt 16 và overload có report SLOW. Thời gian chạy thường
khoảng 6 giây cho normal và 5 giây cho overload; thời gian thực phụ thuộc
lịch chạy hệ điều hành. Demo chỉ phát hiện và giải thích chậm, **chưa tự khắc
phục**, không suy ra hiệu năng UPF thật từ các số đo này.
