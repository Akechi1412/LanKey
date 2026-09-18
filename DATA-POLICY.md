# Dữ liệu của bạn

LanKey là bộ gõ tiếng Việt học thói quen gõ của bạn để gợi ý và tự sửa lỗi. Về mặt kỹ thuật, một bộ gõ nhìn thấy mọi phím bạn bấm — nên tài liệu này nói rõ LanKey giữ lại gì, không giữ gì, để ở đâu, và bạn xoá bằng cách nào. Nội dung này hiển thị nguyên văn trong mục "Dữ liệu của bạn" của ứng dụng và được kiểm tra chéo với mã nguồn.

## LanKey lưu gì

- **Cụm từ bạn gõ, tối đa 5 âm tiết liền nhau** (ví dụ "hệ điều hành windows"), kèm số lần gõ, lần đầu và lần gần nhất. Không lưu gì dài hơn 5 âm tiết, không lưu câu, không lưu thứ tự các cụm với nhau.
- **Cụm bạn tự sửa** (ví dụ bạn xoá "sữa lỗi" rồi gõ lại "sửa lỗi"): cặp sai → đúng và mức tin cậy, để LanKey sửa giúp về sau.
- **Danh sách cấm sửa**: cụm mà bạn đã hoàn tác việc tự sửa hai lần.
- **Cài đặt** của bạn (kiểu gõ, bật/tắt gợi ý và tự sửa, danh sách ứng dụng loại trừ).

## LanKey KHÔNG lưu gì

- Không lưu phím thô, không lưu văn bản hay câu bạn gõ.
- Không lưu bất cứ thứ gì gõ trong **ô mật khẩu** (nhận biết qua Windows UI Automation).
- Không học trong các ứng dụng loại trừ: trình quản lý mật khẩu (KeePass, 1Password, Bitwarden, Dashlane), Remote Desktop, terminal (cmd, PowerShell, Windows Terminal, PuTTY, ssh) và những ứng dụng bạn thêm vào.
- Không học chuỗi có chữ số hoặc ký hiệu, địa chỉ e-mail, số thẻ, số CMND/CCCD, chuỗi dài từ 20 ký tự không có khoảng trắng, hay chuỗi ngẫu nhiên như mật khẩu.
- Không học phần đuôi của một từ khi bạn xoá dở rồi gõ tiếp vào chỗ LanKey không biết.

## Dữ liệu nằm ở đâu

Tất cả trong thư mục `%APPDATA%\LanKey\` trên máy này:

| Tệp | Nội dung | Bảo vệ |
|---|---|---|
| `user_lexicon.enc` | Cụm từ, cụm tự sửa, danh sách cấm sửa | Niêm phong bằng Windows Data Protection (DPAPI) theo tài khoản Windows của bạn. Chỉ tài khoản này trên chính máy này mở được; sao chép sang máy khác hoặc tài khoản khác là vô dụng. Cơ sở dữ liệu chỉ tồn tại dạng đọc được trong bộ nhớ khi LanKey đang chạy. |
| `settings.json` | Cài đặt | Không mã hoá (không chứa nội dung gõ). |
| `lankey.log` | Nhật ký kỹ thuật: thời điểm khởi động, số liệu tổng, lỗi | Không bao giờ chứa phím hay chữ bạn gõ. |

Cụm ít dùng tự bị xoá sau 45–180 ngày không dùng; tổng số cụm không vượt quá 100 000.

## Dữ liệu không rời khỏi máy

LanKey không có mã kết nối mạng. Không gửi, không đồng bộ, không thống kê sử dụng, không kiểm tra cập nhật. Bạn có thể tự kiểm chứng trong mã nguồn (giấy phép GPL-3.0).

## Xoá dữ liệu

- Menu biểu tượng khay → **Xoá toàn bộ dữ liệu đã học**: xoá sạch cụm từ, cụm tự sửa và danh sách cấm sửa, ghi đè tệp ngay lập tức.
- Hoặc thoát LanKey và xoá thư mục `%APPDATA%\LanKey\`.
