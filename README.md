# Smart Gauge B.O.T Racing

เกจดิจิทัลสำหรับ Honda Wave 110i / 125i บนบอร์ด **ESP32-2432S028 (CYD)** จอ 2.8" ILI9341 พร้อม touch

คู่มือนี้พาทำตั้งแต่ดาวน์โหลดโค้ดจาก GitHub จนอัปโหลดลงบอร์ด ทำตามลำดับทีละขั้นได้เลย

---

## สิ่งที่ต้องเตรียม

- คอมพิวเตอร์ Windows หรือ macOS
- บอร์ด ESP32-2432S028 (CYD)
- สาย USB ที่**ส่งข้อมูลได้** (สายชาร์จอย่างเดียวจะมองไม่เห็นบอร์ด)
- อินเทอร์เน็ต สำหรับดาวน์โหลดโปรแกรมและ library

---

## ขั้นที่ 1 ดาวน์โหลดโค้ดจาก GitHub

1. เปิดหน้า https://github.com/namosarawut/smart_gauge_bot
2. กดปุ่มสีเขียว **Code** แล้วเลือก **Download ZIP**
3. แตกไฟล์ ZIP จะได้โฟลเดอร์ชื่อ `smart_gauge_bot-main`
4. **เปลี่ยนชื่อโฟลเดอร์เป็น `smart_gauge_bot`** (ตัด `-main` ออก)
   Arduino IDE บังคับว่าชื่อโฟลเดอร์ต้องตรงกับชื่อไฟล์ `.ino` ถ้าไม่เปลี่ยนจะเปิดไม่ได้
5. ข้างในโฟลเดอร์ต้องมีไฟล์ครบ 2 ไฟล์:
   - `smart_gauge_bot.ino` คือโค้ดหลัก
   - `splash_img.h` คือภาพโลโก้ตอนเปิดเครื่อง (ห้ามลบหรือย้ายออก)

---

## ขั้นที่ 2 ติดตั้ง Arduino IDE

1. ดาวน์โหลด **Arduino IDE 2.x** จากเว็บ arduino.cc หน้า Software
2. ติดตั้งตามขั้นตอนปกติ แล้วเปิดโปรแกรม

> ถ้าเครื่องนี้เคยอัปโหลดโค้ดเกจตัวเดิม (`smart_gauge_all.ino`) ได้แล้ว ข้ามไปขั้นที่ 6 ได้เลย เพราะขั้นที่ 3–5 ตั้งค่าไว้ครบแล้ว

---

## ขั้นที่ 3 ติดตั้งบอร์ด ESP32

1. เมนู **File → Preferences** (macOS: **Arduino IDE → Settings**)
2. ช่อง **Additional boards manager URLs** วางลิงก์นี้ แล้วกด OK:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. เมนู **Tools → Board → Boards Manager**
4. ค้นหา `esp32` แล้วติดตั้ง **esp32 by Espressif Systems**
   - แนะนำเวอร์ชัน **2.0.17** (เลือกเวอร์ชันได้จากกล่องข้างปุ่ม Install) เพราะโค้ดนี้ทดสอบ compile กับเวอร์ชัน 2.0.x
   - ถ้าเครื่องนี้ใช้เวอร์ชันอื่นแล้วอัปโหลดโค้ดตัวเดิมได้ ใช้เวอร์ชันนั้นต่อได้เลย

---

## ขั้นที่ 4 ติดตั้ง Library

เมนู **Tools → Manage Libraries** แล้วค้นหาและติดตั้ง 2 ตัวนี้:

| ค้นหาคำว่า | ติดตั้งตัวที่เขียนว่า |
|---|---|
| `TFT_eSPI` | **TFT_eSPI** by Bodmer |
| `XPT2046` | **XPT2046_Touchscreen** by Paul Stoffregen |

---

## ขั้นที่ 5 ตั้งค่าจอ (User_Setup.h)

Library TFT_eSPI ต้องรู้ว่าจอต่ออยู่ขาไหน โดยอ่านจากไฟล์ `User_Setup.h` ในโฟลเดอร์ library

**ถ้าเคยใช้โค้ดเกจตัวเดิมได้แล้ว:** ใช้ไฟล์ `User_Setup.h` ตัวเดิม ไม่ต้องแก้ ขอแค่ให้มีบรรทัด `#define LOAD_GFXFF` อยู่ในไฟล์ (ส่วนใหญ่มีอยู่แล้ว)

**ถ้าติดตั้งใหม่ทั้งหมด:**

1. หาไฟล์ที่
   - Windows: `Documents\Arduino\libraries\TFT_eSPI\User_Setup.h`
   - macOS: `Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`
2. เปิดด้วยโปรแกรมแก้ข้อความ เช่น Notepad หรือ TextEdit
3. ลบข้อความเดิมทั้งหมด แล้ววางชุดนี้แทน จากนั้นบันทึก:

```cpp
// ESP32-2432S028 (CYD) - Smart Gauge B.O.T Racing
#define USER_SETUP_INFO "CYD_ESP32_2432S028"

#define ILI9341_2_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

#define USE_HSPI_PORT

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       55000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
```

> หมายเหตุ: บอร์ด CYD บางล็อตใช้ชิปจอต่างกันเล็กน้อย ถ้าอัปโหลดแล้วสีเพี้ยนหรือภาพกลับด้าน ให้เปลี่ยน `ILI9341_2_DRIVER` เป็น `ILI9341_DRIVER` แล้วอัปโหลดใหม่

---

## ขั้นที่ 6 เปิดโค้ดและเลือกบอร์ด

1. เมนู **File → Open** แล้วเลือกไฟล์ `smart_gauge_bot/smart_gauge_bot.ino`
   จะเห็นแท็บ `smart_gauge_bot.ino` และ `splash_img.h` ด้านบน
2. เสียบบอร์ดเข้าคอมด้วยสาย USB
3. เมนู **Tools** ตั้งค่าดังนี้:

| ตัวเลือก | ค่า |
|---|---|
| Board | **ESP32 Dev Module** |
| Port | พอร์ตของบอร์ด เช่น `COM5` (Windows) หรือ `/dev/cu.usbserial-xxxx` (macOS) |
| Upload Speed | 921600 (ถ้าอัปโหลดไม่ผ่าน ลด 115200) |
| Partition Scheme | Default 4MB with spiffs |

**ถ้าไม่เจอ Port ของบอร์ด:**
- ลองเปลี่ยนสาย USB เพราะสายชาร์จอย่างเดียวส่งข้อมูลไม่ได้
- ติดตั้งไดรเวอร์ **CH340** (บอร์ด CYD ส่วนใหญ่ใช้ชิปนี้) ดาวน์โหลดจากเว็บผู้ผลิตชิป WCH แล้วถอดเสียบบอร์ดใหม่

---

## ขั้นที่ 7 อัปโหลด

1. กดปุ่ม **✓ Verify** ก่อน เพื่อตรวจว่า compile ผ่าน
2. กดปุ่ม **→ Upload**
3. รอจนขึ้น `Hard resetting via RTS pin...` แปลว่าเสร็จ บอร์ดจะรีสตาร์ทเอง

**ถ้าค้างอยู่ที่ `Connecting......`:** กดปุ่ม **BOOT** บนบอร์ดค้างไว้ จนเริ่มขึ้นตัวเลข % แล้วค่อยปล่อย

---

## ขั้นที่ 8 ตรวจว่าใช้งานได้

| ตรวจ | ผลที่ถูกต้อง |
|---|---|
| เปิดเครื่อง | โลโก้ SMART GAUGE B.O.T RACING ขึ้นเต็มจอ มีแถบโหลดสีฟ้าที่ขอบล่าง ประมาณ 2 วินาที |
| หลังโลโก้ | เข้าหน้า **ENG** หัวจอขวาบนขึ้น **SNIFF** ค่าต่างๆ ขึ้น `--` (ปกติ ดูหัวข้อถัดไป) |
| แตะ tab ด้านล่าง | ENG, SENS, GRPH, DTC, AFR, ECU, TRIP, SYS เปลี่ยนหน้าถูกต้องทุกปุ่ม |
| หน้า SYS | ค่า HEAP MONITOR และ UPTIME ขยับตามเวลา |
| ต่อเข้ารถ + เปิด Serial Monitor (115200) | เห็นข้อมูล byte จาก K-Line วิ่งขึ้นมา |

### ทำไมค่าบนจอขึ้น `--`

โค้ดตั้งไว้ที่ **SNIFF mode** (`KLINE_MODE 1`) ซึ่งฟังสัญญาณ K-Line อย่างเดียว ไม่ส่งข้อมูลไปที่กล่อง ECU เพื่อความปลอดภัย
ค่าจะขึ้นเมื่อถอดรหัสข้อมูลจากรถได้แล้วเท่านั้น จอไม่แสดงค่าปลอม

> ⚠️ **ห้ามเปลี่ยนเป็น `KLINE_MODE 2`** จนกว่าจะได้ค่า protocol ที่ตรวจสอบกับรถจริงแล้ว

---

## แก้ปัญหาที่พบบ่อย

| อาการ | สาเหตุและวิธีแก้ |
|---|---|
| Error `Enable '#define LOAD_GFXFF'` | เปิด `User_Setup.h` แล้วเพิ่มบรรทัด `#define LOAD_GFXFF` (ขั้นที่ 5) |
| Error `splash_img.h: No such file` | ไฟล์ `splash_img.h` ไม่ได้อยู่โฟลเดอร์เดียวกับ `.ino` (ขั้นที่ 1) |
| Error `TFT_eSPI.h` หรือ `XPT2046_Touchscreen.h: No such file` | ยังไม่ได้ติดตั้ง library (ขั้นที่ 4) |
| อัปโหลดผ่านแต่จอดำ | `User_Setup.h` ตั้งขาไม่ตรงกับ CYD (ขั้นที่ 5) |
| จอขาวล้วน | ลองสลับ `ILI9341_2_DRIVER` / `ILI9341_DRIVER` ใน `User_Setup.h` |
| แตะแล้วไปผิดปุ่ม | ค่า calibrate touch ต่างไปตามล็อตบอร์ด เปิด Serial Monitor แล้วแตะมุมจอ จะเห็นบรรทัด `touch raw ... -> ...` ใช้ค่านี้ปรับตัวเลขใน `map(...)` ของฟังก์ชัน `handleTouch()` |
| ค้าง `Connecting......` | กดปุ่ม BOOT ค้างไว้ตอนเริ่มอัปโหลด (ขั้นที่ 7) |

---

## ตั้งค่าเพิ่มเติม (ต้นไฟล์ `smart_gauge_bot.ino`)

| ค่า | ความหมาย |
|---|---|
| `BRAND_1`, `BRAND_2` | ข้อความหัวจอ ค่าเริ่มต้น `B.O.T` และ `RACING` |
| `SPLASH_MS` | เวลาแสดงโลโก้ตอนเปิดเครื่อง หน่วยมิลลิวินาที (2000 = 2 วินาที) |
| `TEMP_WARN_C` | อุณหภูมิที่ตัวเลขเปลี่ยนเป็นสีแดง |
| `KLINE_MODE` | ห้ามเปลี่ยนจาก 1 จนกว่าจะได้ค่า protocol ที่ตรวจแล้ว |

## การต่อสาย K-Line

- GPIO22 (TX) → L9637D TxD
- GPIO27 (RX) ← L9637D RxD (ถ้า RxD เป็น 5V ต้องลดแรงดันด้วยตัวต้านทานแบ่งแรงดัน)
- ไฟ 12V จากรถ → ตัวแปลงไฟ (buck converter) → 5V เข้าบอร์ด ใส่ฟิวส์และ TVS กันไฟกระชาก ต่อ GND ร่วมกัน
