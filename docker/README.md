# Build ReDroid with docker

create build image: `docker build --build-arg userid=$(id -u) --build-arg groupid=$(id -g) --build-arg username=$(id -un) -t android-build-trusty .`
run build image: `docker run -it --rm -v $ANDROID_BUILD_TOP:/src android-build-trusty`

build *ReDroid*:
- `cd /src; . build/envsetup.sh`
- `lunch`
- `m`

sync build (fix file owner / permission etc.)
- `export BUILD_OUT=~/redroid_out && cd vendor/redroid && ./sync.sh` (pasword same as `id -un`)

