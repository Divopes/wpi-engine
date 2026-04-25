var softwareList = [
    {
        name: "7-Zip 23.01",
        cat: "Архиваторы",
        exe: "install\\7z.exe",
        flag: "x64",
        support_xp: false,
        support_7: true,
        install_dflt: true,
        silent_supported: true,
        silent_dflt: true,
        silent_args: "/S",
        normal_args: "",
        lock_silent_choice: false
    },
    {
        name: "WinRAR (Legacy)",
        cat: "Архиваторы",
        exe: "install\\winrar.exe",
        flag: "x86",
        support_xp: true,
        support_7: false, // Игнорируется, так как support_xp: true
        install_dflt: false,
        silent_supported: true,
        silent_dflt: true,
        silent_args: "/S",
        normal_args: "",
        lock_silent_choice: true
    }
];