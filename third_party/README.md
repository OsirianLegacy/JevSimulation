# Vendored dependency

`nlohmann/json.hpp` is the unmodified single-header distribution of nlohmann/json
v3.11.3, from https://github.com/nlohmann/json/tree/v3.11.3/single_include/nlohmann.
The MIT license is included in `nlohmann/LICENSE.MIT`.

It supplies the typed JSON model and strict parser for configuration, goals, and
the C++/Node protocol. Vendoring keeps subsequent builds independent of a new
network fetch. Do not edit the vendor header when changing game schemas.
