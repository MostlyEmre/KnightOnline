# macOS port tooling

Everything for the native Apple Silicon `KnightOnline.app` lives here, outside upstream's tree, so rebasing on upstream stays easy.

- `app/` builds `KnightOnline.app`: `make_app.py` assembles and signs the bundle (and the release zip with `--zip`), `ko-servers/` is the supervisor that starts and stops the bundled servers, and `VERSION` is the app's version.
- `db/` turns OpenKO-db into the seed SQLite database (`openkodb_to_sqlite.py`), writes the game data the client has but OpenKO-db lacks (`port_game_data.py`), and holds the SQLite/SQL Server parity probe.
- `docs/` holds the phase reports. Start with [`docs/phase0-report.md`](docs/phase0-report.md).
- `census/` holds the client platform-API census, the asset fixed-function state scan and the database census, plus the scripts that generate them.
- `spikes/ffspike/` is the fixed-function renderer spike: one scene, a D3D9 backend (run on WARP in CI) and a Metal backend.
- `rigs/` holds the reference rigs. Rig A is Windows/WARP renders (`.github/workflows/port-rig-a-*.yml`, `flip_compare.py`). Rig B is SQL Server on Linux (`.github/workflows/port-rig-b-database.yml`).
