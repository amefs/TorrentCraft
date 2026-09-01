# escape=`
# Build this image with an immutable base-image reference passed through BASE_IMAGE.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

SHELL ["cmd", "/S", "/C"]

# Visual Studio Build Tools requires a .NET Framework base image in Windows
# containers. Do not replace this base with plain windows/servercore.
COPY Install-Toolchain.cmd C:\ci\Install-Toolchain.cmd
RUN C:\ci\Install-Toolchain.cmd

COPY Run-Msvc.cmd C:\ci\Run-Msvc.cmd

WORKDIR C:\work
ENTRYPOINT ["C:\\ci\\Run-Msvc.cmd"]
