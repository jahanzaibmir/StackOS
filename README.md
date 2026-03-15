# Stack OS
StackOS is a small experimental operating system created to better understand how operating systems work at a low level. It is mainly written in C and a bit of assembly, and the goal of the project is to learn how a operating system software can communicate directly with computer hardware. Works on all platforms, virtual box, qemu or on a flash usb drive
The system starts from a simple boot sequence that loads the kernel and initializes core subsystems required for the system to run. These include basic CPU setup, interrupt handling, memory management, and simple hardware drivers. StackOS is designed with a modular structure so different components such as drivers, memory management, and architecture-specific code remain organized and easier to expand. It is primarily a learning project focused on gaining hands-on experience with low-level system programming, operating system architecture, and kernel development.

![StackOS](screenshot.png)

# Requirements

nasm 

gcc

binutils

make

virtualbox 

qemu-system-x86_64 

## Building

*Compile the project:*

```bash
git clone https://github.com/jahanzaibmir/StackOS

cd StackOS

make
```

## Running in QEMU

*First create the disk image:*

```bash
qemu-img create -f raw blizzard.img 64M
```
*Then run the system:*

```bash
make run-gui
```

## Running in VirtualBox

Simply compile the project with ***make*** Then use the generated iso on VirtualBox to boot ***STACK OS***

## Contributing

Contributions are welcome. If you want to improve StackOS, fix bugs, or add new features, feel free to contribuute.

Fork the repository

Create a new branch for your changes

Make your improvements or fixes

Commit your changes with a clear message

Open a Pull Request

Please keep the code simple and follow the existing project structure. Bug reports, suggestions, and improvments are always appreciated.

# Author

Developed by Jahanzaib Ashraf Mir

Built from Scratch 
