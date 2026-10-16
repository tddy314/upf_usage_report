# Demo phát hiện Usage Report chậm khi tải cao

Đây là **giả lập**, không phải UPF thật. Chương trình C dùng pthread trên
Linux hoặc WSL. Phần mô phỏng C không dùng DPDK, PFCP, socket, database hay thư viện ngoài.
“Gửi report” chỉ có nghĩa là hoàn thành xử lý giả lập, không gửi bản tin mạng.

## Giao diện demo trên trình duyệt

### Triển khai lên Render để mở qua Internet

Repository có `render.yaml` (Blueprint) và `Dockerfile`. Docker build chương
trình **C/pthread thật**, rồi chạy `server.py` và `ui.html`. Trình duyệt nhận
số đo từ chương trình C trong container; không thay bằng mô phỏng JavaScript.

1. Đăng nhập Render, chọn **New → Blueprint**.
2. Kết nối repository GitHub `tddy314/upf_usage_report`, chọn nhánh `main`.
   Nếu không thấy repo, cấp quyền cho Render truy cập repo này trong phần
   cấu hình ứng dụng GitHub của Render.
3. Render đọc `render.yaml`. Kiểm tra dịch vụ `upf-usage-report-demo` dùng
   Docker và gói **Free**, rồi chọn **Deploy Blueprint** (hoặc nút xác nhận
   tương ứng trong giao diện Render).
4. Đợi build/deploy hoàn tất, mở URL HTTPS do Render cấp trên trang dịch vụ.
   Chọn Normal hoặc Overload và bấm Chạy mô phỏng.

Không cần database, secret hay nhập lệnh build/start riêng khi dùng Docker.
Server đọc `PORT`, lắng nghe `0.0.0.0`, kiểm tra sức khỏe qua `/healthz`.
`RENDER_EXTERNAL_URL` do Render cấp tự động được dùng để chấp nhận các lần
bấm chạy từ đúng giao diện công khai. Nếu thêm tên miền riêng, cần cập nhật
cấu hình origin; bản hiện tại dùng URL mặc định của Render.

Gói miễn phí có thể ngủ khi không hoạt động; mở app và chạy thử trước khi
quay video để chờ khởi động xong. Mỗi container chỉ chạy một kịch bản tại một
thời điểm; người truy cập khác sẽ được báo đang bận. Mọi người có URL đều có
thể dùng demo. Đây là server demo nhỏ, không có đăng nhập hay lưu dữ liệu.

Có thể kiểm tra container trên máy có Docker:

```sh
docker build -t upf-usage-report-demo .
docker run --rm -p 8000:10000 -e PORT=10000 \
  -e RENDER_EXTERNAL_URL=http://localhost:8000 upf-usage-report-demo
```

Sau đó mở `http://localhost:8000`. Dừng container bằng Ctrl+C.

### Chạy cục bộ

Giao diện bổ sung dùng Python 3 (thư viện chuẩn, không cần pip) để chạy chương
trình C và truyền từng dòng kết quả về trình duyệt qua HTTP cục bộ. Không cần
Node.js, CDN hay kết nối Internet. Logic mô phỏng và phép đo vẫn nằm trong C.

```sh
make ui
```

Trên máy Linux/WSL đang chạy lệnh, mở trình duyệt tại địa chỉ
`http://localhost:8000`. Chọn **Normal** hoặc **Overload** rồi bấm **Chạy mô phỏng**.
Trong WSL, có thể dùng trình duyệt Windows với localhost forwarding được bật.
Nếu dùng workspace cloud, lệnh này chạy trên máy cloud; localhost trên máy cá
nhân không trỏ đến workspace đó. Có thể clone repo về Linux/WSL để trình diễn.
Nhấn Ctrl+C trong terminal để dừng web server.

Giao diện có biểu đồ cột chồng (chờ queue + xử lý), đường ngưỡng 300 ms,
bảng từng report, số SLOW/DROPPED, log gốc và tổng kết sau khi cả hai luồng kết
thúc. Các bộ đếm trong lúc chạy chỉ phản ánh report đã có kết quả, không phải
độ dài queue tức thời. Queue lớn nhất và số đã tạo được lấy từ tổng kết C.
Trục dọc tự điều chỉnh; so sánh số ms khi chuyển kịch bản.

Server chỉ lắng nghe loopback cổng 8000, cho chạy một mô phỏng tại một thời
điểm và giới hạn mỗi lần chạy 20 giây. `server.py` và `ui.html` là hai file
bổ sung cho giao diện; cách chạy bằng terminal bên dưới vẫn dùng được.

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
