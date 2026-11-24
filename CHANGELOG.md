# Changelog

## 0.1.0 (2026-11-24)


### Features

* adopt Waveshare ESP32-S3-RLCD-4.2 and wake-sound plan ([3b15bcf](https://github.com/braveadmin/inkwake/commit/3b15bcf3eda8861e2c092ee423d184ba0b187a15))
* **alarms:** F1 multiple alarms with speaker beep, snooze and setup page ([#2](https://github.com/braveadmin/inkwake/issues/2)) ([#42](https://github.com/braveadmin/inkwake/issues/42)) ([fa36d40](https://github.com/braveadmin/inkwake/commit/fa36d40db14a36bd59dbac2d3a40de8313248ca7))
* battery-first hardware with Waveshare 4.2in panel ([47258f2](https://github.com/braveadmin/inkwake/commit/47258f23111849108f25b76599c06144668f78f3))
* **battery:** low-battery icon on the clock face ([#36](https://github.com/braveadmin/inkwake/issues/36)) ([#37](https://github.com/braveadmin/inkwake/issues/37)) ([c59ac9c](https://github.com/braveadmin/inkwake/commit/c59ac9c4738da46442b596f92442b5c9081f952d))
* **ntp:** F12 NTP sync over Wi-Fi ([#13](https://github.com/braveadmin/inkwake/issues/13)) ([#30](https://github.com/braveadmin/inkwake/issues/30)) ([518b1ce](https://github.com/braveadmin/inkwake/commit/518b1ceddcd9d25c0fada3fa83b4f664d0917203))
* **temp:** F11 room temperature from the on-board SHTC3 ([#26](https://github.com/braveadmin/inkwake/issues/26)) ([519a80d](https://github.com/braveadmin/inkwake/commit/519a80d2d5dc30e238bb3d967b905872b639d28e)), closes [#12](https://github.com/braveadmin/inkwake/issues/12)
* **tz:** F12 timezone, local time on the face ([#28](https://github.com/braveadmin/inkwake/issues/28)) ([7b79e98](https://github.com/braveadmin/inkwake/commit/7b79e98aa366f6e8724cf9c1220729d11321ad66))
* **wifi:** remember up to five Wi-Fi networks ([#33](https://github.com/braveadmin/inkwake/issues/33)) ([#40](https://github.com/braveadmin/inkwake/issues/40)) ([f411adf](https://github.com/braveadmin/inkwake/commit/f411adf021ebca08ae91c24944fb8914c2653039))
* **wifi:** Wi-Fi provisioning with setup network and captive page ([#20](https://github.com/braveadmin/inkwake/issues/20)) ([#35](https://github.com/braveadmin/inkwake/issues/35)) ([5b96639](https://github.com/braveadmin/inkwake/commit/5b96639a9c6228717593a02b5ced1d57132d84bf))


### Bug Fixes

* **display:** ST7305 power-mode timing, no per-wake mode switch ([#25](https://github.com/braveadmin/inkwake/issues/25)) ([#38](https://github.com/braveadmin/inkwake/issues/38)) ([e8a4c41](https://github.com/braveadmin/inkwake/commit/e8a4c418949cd7ab572645e2037403fd1ab2821e))
* **firmware:** pin pioarduino 55.03.313 ([#24](https://github.com/braveadmin/inkwake/issues/24)) ([20a7ca8](https://github.com/braveadmin/inkwake/commit/20a7ca80b309daca3dbc39935ee74f871bab93a5))
* **rtc:** one wake per minute while the time is not set ([#27](https://github.com/braveadmin/inkwake/issues/27)) ([#31](https://github.com/braveadmin/inkwake/issues/31)) ([593c66b](https://github.com/braveadmin/inkwake/commit/593c66bd7e82673eb68716bcac836a8a43cd6ccc))
