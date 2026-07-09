using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;

// Il target net10.0-windows... rende l'assembly Windows-only, ma con
// GenerateAssemblyInfo=false (sopra, per via di Version.g.cs) l'SDK non genera
// più da solo [assembly: SupportedOSPlatform]. Senza, l'analyzer CA1416 non sa
// che l'intero assembly gira solo su Windows e segnala ogni chiamata WinForms
// come "raggiungibile da tutte le piattaforme". Va dichiarato qui a mano.
[assembly: SupportedOSPlatform("windows")]

// Le informazioni generali relative a un assembly sono controllate dal seguente 
// set di attributi. Modificare i valori di questi attributi per modificare le informazioni
// associate a un assembly.
[assembly: AssemblyTitle("RTVirtual Camerda")]
[assembly: AssemblyDescription("")]
[assembly: AssemblyConfiguration("")]
[assembly: AssemblyCompany("")]
[assembly: AssemblyProduct("RTVirtual Camerda")]
[assembly: AssemblyCopyright("Copyleft ©  2025")]
[assembly: AssemblyTrademark("")]
[assembly: AssemblyCulture("")]

// Se si imposta ComVisible su false, i tipi in questo assembly non saranno visibili
// ai componenti COM. Se è necessario accedere a un tipo in questo assembly da
// COM, impostare su true l'attributo ComVisible per tale tipo.
[assembly: ComVisible(false)]

// Se il progetto viene esposto a COM, il GUID seguente verrà utilizzato come ID della libreria dei tipi
[assembly: Guid("a0abfde8-6672-48c1-8143-fd4acb2241c7")]

// Le versioni (AssemblyVersion/AssemblyFileVersion/AssemblyInformationalVersion)
// sono generate da git in build/Version.g.cs (vedi build/Set-GitVersion.ps1).
