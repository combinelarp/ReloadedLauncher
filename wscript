# Wscript
def options(opt):
    opt.load('compiler_cxx')

def configure(conf):
    conf.load('compiler_cxx')
    # Windows flags
    conf.env.append_unique('CXXFLAGS', ['/std:c++17', '/W3', '/DWIN32_LEAN_AND_MEAN', '/D_CRT_SECURE_NO_WARNINGS', '/EHsc'])
    conf.env.append_unique('LINKFLAGS', ['/SUBSYSTEM:WINDOWS', '/ENTRY:WinMainCRTStartup'])
    # Libraries
    conf.env.LIB = ['gdiplus', 'wininet', 'shell32', 'ole32', 'shlwapi', 'ws2_32', 'advapi32', 'user32', 'gdi32']

def build(bld):
    # Compile resources
    bld(rule='rc /fo ${TGT} ${SRC}', source='src/resource.rc', target='src/resource.res')
    
    # Recurse into source subdirectories
    bld.recurse('src')
    
    # Link all objects into the executable
    bld.program(
        target='BlockmanGO',
        source=bld.path.ant_glob('src/**/*.cpp'),
        includes=['.', 'src'],
        use=['src/resource.res']
    )