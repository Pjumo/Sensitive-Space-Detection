# PIR Driver 사용 방법

## 1. Ubuntu에서 드라이버 빌드

```bash
cd /srv/nfs/Sensitive-Space-Detection/drivers/pir/src
```

```bash
make KDIR=/home/ubuntu/pi_bsp/kernel/linux \
ARCH=arm \
CROSS_COMPILE=arm-linux-gnueabihf-
```

빌드 결과:

```text
pir_driver.ko
pir-presence.dtbo
```

## 2. Device Tree Overlay 설치

`pir-presence.dtbo`를 처음 만들었거나 수정했을 때 Raspberry Pi에서 실행한다.

```bash
cd /mnt/ubuntu_nfs/Sensitive-Space-Detection/drivers/pir/src
```

```bash
sudo cp pir-presence.dtbo /boot/firmware/overlays/
```

## 3. PIR 드라이버 실행

```bash
cd /mnt/ubuntu_nfs/Sensitive-Space-Detection/drivers/pir/src
```

```bash
sudo dtoverlay pir-presence
sudo insmod pir_driver.ko
```

드라이버가 올라가면 `/dev/pir_presence` 장치 파일이 생성된다.

## 4. 테스트 프로그램 빌드

```bash
cd /mnt/ubuntu_nfs/Sensitive-Space-Detection/drivers/pir/user
```

```bash
make
```

## 5. PIR 센서 테스트

```bash
./pir_test
```

PIR 센서 앞에서 움직이면 감지 이벤트가 출력된다.

테스트 종료:

```text
Ctrl + C
```

## 6. 드라이버 종료

```bash
cd /mnt/ubuntu_nfs/Sensitive-Space-Detection/drivers/pir/src
```

```bash
sudo rmmod pir_driver
sudo dtoverlay -r pir-presence
```

## 7. 다시 실행할 때

Overlay 파일을 이미 설치한 상태라면 다음 명령만 실행한다.

```bash
cd /mnt/ubuntu_nfs/Sensitive-Space-Detection/drivers/pir/src
sudo dtoverlay pir-presence
sudo insmod pir_driver.ko

cd ../user
./pir_test
```

## 8. 테스트 프로그램 다시 빌드

```bash
cd /mnt/ubuntu_nfs/Sensitive-Space-Detection/drivers/pir/user
```

```bash
make clean
make
```

## 9. Git 반영

```bash
cd /mnt/ubuntu_nfs/Sensitive-Space-Detection
```

```bash
git add drivers/pir/src/README.md
git commit -m "docs: add PIR driver commands"
git push origin main
```
