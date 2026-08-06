# Project Layout

칸반 보드(시작 전 20 / 진행중 4) 항목을 폴더 구조에 매핑한 결과입니다.
각 폴더 안 README.md에 해당 폴더가 담당하는 칸반 카드가 적혀 있습니다.

```
.
├── docs/
│   ├── direction/        # Device Driver·UserSpace 이론 및 방향성, 사용자 앱 단 방향성
│   ├── comparison/       # PIR vs Radar
│   ├── porting/          # FRDM-IMX93으로 프로젝트 이전
│   └── git/              # git 협업 규칙
├── firmware/
│   └── iwr6843/          # IWR6843 펌웨어 플래시, cfg 프로파일
├── drivers/
│   ├── pir/
│   │   ├── src/          # PIR 드라이버 구현/완성/실물 연동
│   │   └── user/         # PIR 드라이버 raw 테스트 유틸
│   └── radar/
│       ├── src/          # Radar 드라이버 구현/완성/실물 연동, 로깅·디버깅
│       ├── mock/         # RADAR mock 드라이버, mock 통합 데모
│       └── user/         # Radar 드라이버 raw 테스트 유틸
├── bridge/
│   ├── src/               # RADAR 브릿지 데몬 초안, 브릿지+드라이버 통합
│   └── algo/               # Radar 알고리즘 구현 (DBSCAN/칼만/MUSIC)
├── common/
│   ├── include/            # PIR/Radar 공유 헤더 (sensor_frame_header, ioctl)
│   └── sensor_backend/      # UserSpace + Device Driver 공용 추상화 라이브러리
├── app/
│   ├── sensor_agent/        # sensor_agent
│   └── socket_server/       # 센서 TCP 서버 + 웹 대시보드 브릿지 (Node.js, REST/WebSocket)
├── tools/
│   └── demo/                # Radar mock 통합 데모, PIR/Radar 실물 연동 검증
├── yocto/                   # Yocto 툴체인으로 리눅스 환경 구성
└── build/                   # 빌드 시스템 제작
```

## 칸반 카드 → 폴더 매핑

| 칸반 카드 | 폴더 |
|---|---|
| *&UserSpace + Device Driver | (상위 에픽) docs/direction/, common/sensor_backend/ |
| *Device Driver 이론 및 방향성 설정 | docs/direction/ |
| &UserSpace 이론 및 방향성 설정 | docs/direction/ |
| 사용자 앱 단 방향성 | docs/direction/ |
| git | docs/git/ |
| *PIR 드라이버 구현 / 완성 | drivers/pir/src/ |
| &PIR 드라이버 실물 연동 | drivers/pir/src/, tools/demo/ |
| *RADAR mock 드라이버 제작 | drivers/radar/mock/ |
| &Radar mock 통합 데모 | drivers/radar/mock/, tools/demo/ |
| *IWR6843 펌웨어 플래시 | firmware/iwr6843/ |
| *RADAR 브릿지 데몬 초안 | bridge/src/ |
| Radar 드라이버 구현 / *완성 | drivers/radar/src/ |
| &Radar 실물 연동 | drivers/radar/src/, tools/demo/ |
| *브릿지 데몬 + 드라이버 통합 | bridge/src/ + drivers/radar/src/ (교차) |
| *Device Driver 로깅/디버깅 | drivers/pir/src/, drivers/radar/src/ |
| &Radar 알고리즘 구현 | bridge/algo/ |
| &빌드 시스템 제작 | build/ |
| &sensor_agent | app/sensor_agent/ |
| &C소켓 서버 프로토타입 → 웹 대시보드 브릿지로 교체 | app/socket_server/ |
| &Yocto 툴체인으로 리눅스 환경 구성 | yocto/ |
| *&FRDM-IMX93으로 프로젝트 이전 | docs/porting/ |
| *&PIR vs Radar | docs/comparison/ |

## 담당 원칙 (지난 논의 기준)

- 폴더별 담당자가 자유롭게 커밋/merge. 단 `common/include/`, `common/sensor_backend/`는 PIR·Radar 양쪽에 다 영향을 주므로 변경 시 교차 리뷰 필수.
- `bridge/`와 `drivers/radar/src/`는 통합 작업(브릿지 데몬 + 드라이버 통합 카드) 시점에 양쪽 담당자가 같이 작업.
