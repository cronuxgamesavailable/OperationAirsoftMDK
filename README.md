# Operation Airsoft MDK (Mod Development Kit) 🎯

Welcome to the official **Operation Airsoft Mod Development Kit (MDK)** v1.4 – a powerful toolset for creating, describing, and exporting mods for **Operation Airsoft**, an airsoft first-person shooter powered by Unreal Engine 5.

> ⚠️ **Note:** This repository contains *the plugin and templates*.  
> You can integrate it into your own Unreal Engine 5.4 project to enable mod packaging features.  
> This is **not** the full game or a standalone modding environment.

---

## 📦 Features

- ✅ **Mod Packaging UI** – Built directly into the UE5 Editor
- 🔍 **Auto Asset Detection** – Automatically lists available Maps and Meshes
- 📁 **Clean Output Structure** – Copies cooked files and metadata into organized mod folders
- 🔥 **Cooking Support** – Cook selected assets before packaging directly from the UI  
- 📦 **.PAK packaging support** - Package and run the mods you create within Operation Airsoft!
- ⚙️ **Mod Types** – Maps, Layouts and Attachments (Check todo for future plans)

---

## 📝 TODO

These are planned or upcoming features for the MDK:

- ⚙️ Support for Multiple Mod Types – Add Clothes as a template.
- 🧪 Add mod validator (e.g. check for missing assets, naming issues)
- ☁️ Steam Workshop integration (upload mods directly)
- 💬 Localization/translation support for mod metadata

---

## 📥 Install Guide

Follow these simple steps to set up the MDK in your Unreal Engine project:

(Video Tutorials - https://www.youtube.com/playlist?list=PLOnU8Ej5ADnX6cwLcu_SOZ8BUCi-3V6N5)

1. **Create a new Project** - Name it **ModMaker** (VERY Important)
2. **Find your Project Root** – This is the folder containing your `.uproject` file.  
3. **Copy the Plugin** – Place the **ModCreator** folder into: ProjectRoot/Plugins/
4. **Copy the Extra Content Plugin** - Place **extracontent** folder into: ProjectRoot/Plugins/
5. **Restart Unreal Engine** – The plugin will now appear in the Editor under the Mod Tools section.
6. **Open the Project Settings** - Disable `Share Material Shader Code` and thats it!

---

## 🙌 Stay Involved

Have an idea to improve the MDK?  
Found a bug or want to contribute?  
We'd love to hear from you!

Happy modding!
