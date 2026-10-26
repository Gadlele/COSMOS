# Licensing

COSMOS's own code is under the GNU General Public License v3.0 or later ([LICENSE](LICENSE)). The code it includes
from others keeps its own licence, which the GPL allows:

| What | Where | Licence |
| --- | --- | --- |
| The FM-1 platform: hardware layer, WiFi, build, web installer | `base/` (a git submodule) | MPL-2.0, see `base/LICENSING.md` |
| The HTTPS connection (`probe/net.c`), adapted from fm1-radio's `net/radio_net.c` | `probe/net.c` | MPL-2.0 |
| BearSSL, with fm1-radio's yields to WiFi's tasks in `src/ec/ec_p256_m31.c` and `src/inner.h` | `third_party/bearssl/` | MIT (`LICENSE.txt`) |
| Terminus font | `base/assets/fonts/` | SIL OFL 1.1 |
| Michroma font (the splash's word) | `third_party/fonts/michroma/` | SIL OFL 1.1 (`OFL.txt`) |

Read from the JieLi AC79 SDK at build time, not in this tree: JieLi's WiFi libraries and lwIP (Apache-2.0 and BSD),
from <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>.

## Data

The stars (`cosmos/data/stars.csv`) are the 500 brightest of the Yale Bright Star Catalogue, 5th edition (Hoffleit
and Warren, 1991), public domain. The planets' orbits are JPL's approximate Keplerian elements (Standish).


The weather comes from [Open-Meteo](https://open-meteo.com) (CC BY 4.0), the Kp index from the NOAA Space Weather
Prediction Center (public domain).
