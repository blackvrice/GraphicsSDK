# Independent integration

설치 후 소비자는 다음만 사용한다.

```cmake
cmake_minimum_required(VERSION 3.25)
project(MyApplication LANGUAGES CXX)
find_package(GraphicsSDK CONFIG REQUIRED)
add_executable(MyApplication main.cpp)
target_link_libraries(MyApplication PRIVATE GraphicsSDK::Core)
```

`tests/consumer`는 SDK source를 add_subdirectory하지 않는 별도 CMake 프로젝트다.
설치 디렉터리만 CMAKE_PREFIX_PATH로 전달해 공개 헤더·정적 라이브러리·system link
dependencies가 충분한지 검증한다. Windows 소비자와 SDK의 architecture 및 CRT
설정은 일치시켜야 한다. 기본 Visual Studio Release 설정은 x64 /MD다.

version은 초기 개발용 0.0.1이며 동일 버전 package만 호환으로 선언한다.
아직 ABI freeze나 v0.1 전체 backend 지원을 의미하지 않는다.
별도 GitHub 저장소 생성/원격 배포는 이 작업에서 수행하지 않는다.
