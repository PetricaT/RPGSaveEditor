function Component() {}

Component.prototype.createOperations = function () {
    component.createOperations();

    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut",
            "@TargetDir@/RPGSaveEditor.exe",
            "@StartMenuDir@/RPGSaveEditor.lnk",
            "workingDirectory=@TargetDir@");
        component.addOperation("CreateShortcut",
            "@TargetDir@/RPGSaveEditor.exe",
            "@DesktopDir@/RPGSaveEditor.lnk",
            "workingDirectory=@TargetDir@");
    } else if (systemInfo.kernelType === "linux") {
        component.addOperation("CreateDesktopEntry",
            "RPGSaveEditor.desktop",
            "Type=Application\nName=RPGSaveEditor\nComment=A local RPGMaker save editor\nExec=@TargetDir@/RPGSaveEditor\nIcon=@TargetDir@/icon.png\nTerminal=false\nCategories=Utility;");
    }
}
