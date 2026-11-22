#include "Shells.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QSet>
#include <QRegularExpression>
#include <QSettings>

QVector<Shell> detectShells(){
    QVector<Shell> list;QSet<QString> seen;
    auto add=[&](QString path,QString label={}){
        if(path.isEmpty())return;
        QFileInfo info(path);
        if(!info.isFile()||!info.isExecutable())return;
        auto absolute=info.absoluteFilePath();
        // Preserve argv[0] (e.g. /bin/sh -> dash) instead of canonicalizing it.
        auto key=absolute;
#ifdef Q_OS_WIN
        key=key.toLower();
#endif
        if(seen.contains(key))return;seen.insert(key);
        auto name=info.completeBaseName();if(label.isEmpty())label=name;
        QStringList args;
#ifdef Q_OS_MACOS
        if(name=="bash"||name=="zsh")args<<"-l";
#endif
        list.append({key,label,absolute,args,{}});
    };
#ifdef Q_OS_WIN
    auto system=qEnvironmentVariable("SystemRoot","C:/Windows");
    add(system+"/System32/cmd.exe","Command Prompt");
    add(QStandardPaths::findExecutable("pwsh"),"PowerShell");
    for(const auto& dir:QDir(qEnvironmentVariable("ProgramFiles")+"/PowerShell").entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot))
        add(dir.absoluteFilePath()+"/pwsh.exe","PowerShell "+dir.fileName());
    add(system+"/System32/WindowsPowerShell/v1.0/powershell.exe","Windows PowerShell");
    for(auto name:{"bash","zsh","fish","nu","sh"})add(QStandardPaths::findExecutable(name));
    add(qEnvironmentVariable("ProgramFiles")+"/Git/bin/bash.exe","Git Bash");
    const auto wsl=QStandardPaths::findExecutable("wsl.exe");
    if(!wsl.isEmpty()){
        // Registered distributions are visible without starting any VM.
        QSettings registry("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss",QSettings::NativeFormat);
        for(const auto& group:registry.childGroups()){
            auto name=registry.value(group+"/DistributionName").toString();
            if(!name.isEmpty()&&!name.startsWith('-')&&name.size()<=256)
                list.append({"wsl:"+name,"WSL · "+name,wsl,{"--distribution",name},name});
        }
    }
#else
    add(qEnvironmentVariable("SHELL"));
    QFile file("/etc/shells");
    if(file.open(QIODevice::ReadOnly))for(auto row:file.readAll().split('\n')){
        auto path=QString::fromUtf8(row).trimmed();
        if(path.startsWith('/')&&!path.endsWith("/nologin")&&!path.endsWith("/false"))add(path);
    }
    for(auto name:{"bash","zsh","fish","sh","dash","ksh","tcsh","csh","nu","pwsh","elvish"})add(QStandardPaths::findExecutable(name));
#endif
    return list;
}
QVector<Shell> wslShells(const QByteArray& data,QString exe){
    QString decoded;
    if(data.contains('\0')){
        QStringDecoder decode(QStringDecoder::Utf16LE);decoded=decode(data);
    }else decoded=QString::fromUtf8(data);
    decoded.remove(QChar(0xfeff));decoded.remove(QChar(u'\0'));
    QVector<Shell> result;
    for(auto line:decoded.split('\n')){
        line=line.trimmed();if(line.isEmpty()||line.size()>256||line.startsWith('-')||line.contains(QChar(0xfffd)))continue;
        result.append({"wsl:"+line,"WSL · "+line,exe,{"--distribution",line},line});
        if(result.size()>=64)break;
    }
    return result;
}
term::Launch launchFor(const Shell& s,QString cwd,int rows,int cols){
    term::Launch launch;launch.executable=s.executable.toUtf8().toStdString();
    QStringList arguments=s.arguments;
    launch.rows=rows;launch.cols=cols;
    launch.environment={"TERM=xterm-256color","COLORTERM=truecolor","TERM_PROGRAM=TerminalEmulator","TERM_PROGRAM_VERSION=1.0.0"};
#ifdef Q_OS_WIN
    auto name=QFileInfo(s.executable).completeBaseName().toLower();
    if(!s.distribution.isEmpty()){
        arguments<<"--cd"<<(cwd.startsWith('/')?cwd:QString("~"));cwd=QDir::homePath();
    }else if(name=="cmd"){
        // Prompt-local integration, no files or shell profile are modified.
        auto prompt=qEnvironmentVariable("PROMPT","$P$G");
        launch.environment.push_back((QStringLiteral("PROMPT=$E]777;$P$E\\")+prompt).toUtf8().toStdString());
    }else if(name=="pwsh"||name=="powershell"){
        arguments<<"-NoExit"<<"-Command"<<QStringLiteral(
            "$global:AppSuiteOriginalPrompt = $function:prompt; "
            "function global:prompt { "
            "try { [Console]::Write(([char]27).ToString() + ']777;' + "
            "$ExecutionContext.SessionState.Path.CurrentFileSystemLocation.Path + ([char]27).ToString() + '\\') } catch {}; "
            "if ($global:AppSuiteOriginalPrompt) { & $global:AppSuiteOriginalPrompt } else { 'PS> ' } }");
    }
#endif
    if(cwd.isEmpty()||!QFileInfo(cwd).isDir())cwd=QDir::homePath();
    launch.cwd=cwd.toUtf8().toStdString();
    for(const auto& a:arguments)launch.arguments.push_back(a.toUtf8().toStdString());
    return launch;
}
