<p align="center" style="background-color:#14191e;padding:28px 16px;border-radius:12px;">
  <img alt="PrismServer" src="logo/logo-textDark.svg" width="420">
</p>

<p align="center">
  PrismServer launches Minecraft servers from one window.<br>
  Create a server, install plugins, and run it from the console.
</p>

## Download

The Windows build is a single file. Get [PrismServer.exe](https://github.com/STG-SERIES/PrismServer/releases/latest) from the latest release and open it. Qt and the Visual C++ redistributable do not need to be installed.

The first launch unpacks the app to `%LOCALAPPDATA%\PrismServer\app`. Later launches reuse that copy. Servers, settings, and logs are stored in `%APPDATA%\PrismServer`.

Internet is required to download server software and Java.

## Servers

- **Paper** and **Purpur** game servers
- **Velocity** proxy, including which backend a player joins first
- Plugins from Modrinth, with enable and disable from the instance
- Live console output, and commands sent to the running server
- A first-start prompt for the Minecraft EULA, port, online mode, player limit, and memory
- A Java download when the computer does not already have a new enough version

## License

PrismServer is a fork of [Prism Launcher](https://prismlauncher.org). The launcher code is licensed under [GPL-3.0](LICENSE). The logo and related assets are under CC BY-SA 4.0. PrismServer is not endorsed by Prism Launcher or MultiMC.
