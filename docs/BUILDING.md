# Building

The JieLi toolchain runs on Linux x86-64. On a Mac, fm1-base's build runs its tools in Docker (`base/README.md`).

## Once

```
git clone --recursive https://github.com/Gadlele/COSMOS.git
cd COSMOS
base/tools/get_toolchain.sh                      # JieLi's Linux toolchain, into ~/.jieli/toolchain
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
pip3 install pillow mido python-rtmidi
```

The SDK is large. If the clone breaks off, a sparse one with only what the build reads works:

```
git clone --depth 1 --filter=blob:none --sparse --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
cd ~/fw-AC79_AIoT_SDK && git sparse-checkout set --no-cone include_lib apps/common/include apps/common/config \
    apps/common/ble/include apps/demo/demo_ble/include cpu/wl82/liba cpu/wl82/tools lib/net/lwip_2_1_3
```

## Build

```
./build.sh cosmos                    # cosmos/build/cosmos.fwsc, a development build
./build.sh probe                     # probe/build/probe.fwsc, the test firmware
./build.sh probe --release 0.2       # probe/build/probe-0.2.fwsc, identity FM-1_71000200
./build.sh probe --host              # the host simulator: probe/build/host/probe_host SCRIPT OUTDIR
```

## Install

From the command line:

```
python3 base/tools/fm1_install.py probe/build/probe.fwsc
```

Or from the browser: the site puts the firmware's installer at `install/` and the probe's at `probe/install/`:

```
./build.sh cosmos --release 0.1 && ./build.sh probe --release 0.2
tools/make_pages.py cosmos cosmos/build/cosmos-0.1.fwsc 0.1 OUT
tools/make_install.py probe probe/build/probe-0.2.fwsc 0.2 OUT/probe && cp OUT/probe/words/*.json OUT/words/
```

The `gh-pages` branch serves it at <https://gadlele.github.io/COSMOS/>.

Installing firmware is at your own risk. Hold OCT− and OCT+ for 5 seconds for the update mode; the installer page
also puts M-VAVE's own firmware back.
