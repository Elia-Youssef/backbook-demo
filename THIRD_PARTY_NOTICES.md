# Third-Party Notices

Backbook uses the following third-party components. Each component remains
subject to its own license.

## Native dependencies

| Component | Pinned version or revision | Use | License |
| --- | --- | --- | --- |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | 0.51.0, revision `d66d9a95997d51a8ba9822a611d1267757741535` | HTTP server | MIT |
| [JSON for Modern C++](https://github.com/nlohmann/json) | 3.12.0, revision `65ee68451d8eb2b5f3a30b410476ab83deb3289b` | JSON parsing and serialization | MIT |
| [GoogleTest](https://github.com/google/googletest) | 1.17.0, revision `52eb8108c5bdec04579160ae17225d66034bd723` | Native tests only | BSD-3-Clause |

CMake verifies the downloaded cpp-httplib and JSON for Modern C++ archives
against SHA-256 values recorded in `CMakeLists.txt`.

## Frontend runtime dependencies

| Component | Pinned version | License |
| --- | --- | --- |
| [React](https://github.com/facebook/react) | 19.2.8 | MIT |
| [React DOM](https://github.com/facebook/react) | 19.2.8 | MIT |

## Frontend development dependencies

| Component | Pinned version | License |
| --- | --- | --- |
| [Tailwind CSS](https://github.com/tailwindlabs/tailwindcss) | 4.3.3 | MIT |
| [Tailwind CSS Vite integration](https://github.com/tailwindlabs/tailwindcss) | 4.3.3 | MIT |
| [Vite](https://github.com/vitejs/vite) | 8.1.5 | MIT |
| [React plugin for Vite](https://github.com/vitejs/vite-plugin-react) | 6.0.4 | MIT |
| [Vitest](https://github.com/vitest-dev/vitest) | 4.1.10 | MIT |
| [TypeScript](https://github.com/microsoft/TypeScript) | 7.0.2 | Apache-2.0 |
| [React type definitions](https://github.com/DefinitelyTyped/DefinitelyTyped) | 19.2.17 | MIT |
| [React DOM type definitions](https://github.com/DefinitelyTyped/DefinitelyTyped) | 19.2.3 | MIT |

Exact frontend dependency resolution, including transitive packages, is
recorded in `web/package-lock.json`.
