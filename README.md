# RPS

RPS is a database-driven Rock/Paper/Scissors battle simulator. Players can create an account, upload assembly bots, choose three compiled bots, and run a match on the simulation grid.

The program is designed for Windows and Linux.

## Features

- Login and account creation
- Assembly upload and compilation
- Database-backed bot storage and selection
- Automatic Rock/Paper/Scissors battle simulation
- Final match statistics and flow restart

## Build

### Windows

Run `build.bat` from the repository root. This will automatically:
1. Compile the teenyat assembler (tnasm)
2. Build the RPS executable
3. Place both in `build/out/bin/`

### Linux

Run `build.sh` from the repository root.

## Launch

After building, launch RPS from the build output folder:

**Windows:**
```cmd
build\out\bin\RPS.exe
```

**Linux:**
```bash
build/out/bin/RPS
```

RPS starts at the database/login flow screen.

## Documentation

- Program guide: [docs/rps.md](docs/rps.md)
- Assembly instruction set guide: [docs/instruction-set.md](docs/instruction-set.md)
- Official TeenyAT instruction docs: https://github.com/miniat-amos/TeenyAT/tree/main/docs

## Runtime Requirements

- CMake (3.14+)
- A C++ toolchain (GCC/G++ or Clang, not MSVC)
- OpenGL support
- On Linux: X11 development libraries and Mesa/OpenGL development libraries
- PostgreSQL client tools available on PATH (including `psql`)

## Configuration

RPS reads database credentials from environment variables. If a `.env` file is present, it will be loaded at startup.

### Environment Variables

- `DB_HOST`: database host (example: localhost)
- `DB_PORT`: database port (example: 5432)
- `DB_NAME`: database name (example: rps)
- `DB_USER`: database user (user with restricted access)
- `DB_PASS`: database password

### .env Setup (Recommended)

Create a `.env` file in `build/out/bin/` and add:

```env
DB_HOST=localhost
DB_PORT=5432
DB_NAME=rps
DB_USER=database_username
DB_PASS=database_password
```

**Notes:**
- Do not wrap values in quotes unless you need literal leading/trailing spaces
- Keep `.env` out of version control if it contains real credentials
- The `.env` file must be in the same directory as the RPS executable

## Usage

1. Start the program from the build output folder
2. Log in or create an account
3. Upload a `.asm` bot file
4. Choose Rock, Paper, and Scissors bot versions
5. Run the match and review the results