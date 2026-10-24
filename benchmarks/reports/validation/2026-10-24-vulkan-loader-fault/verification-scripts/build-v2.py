import pathlib
source=pathlib.Path(__file__).with_name('build.py').read_text()
source=source.replace("v/(name+'.log')","v/(name+'-v2.log')").replace("v/'build-commands.json'","v/'build-commands-v2.json'").replace("folder='out/vulkan-loader-fault-'+variant","folder='out/vulkan-loader-fault-'+variant+'-v2'")
exec(compile(source,str(__file__),'exec'))
