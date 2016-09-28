import sys
import glob
import os.path

# attempt to import zabbix runtime if running embedded
try:
  import zabbix_runtime
except ImportError:
  zabbix_runtime = None

__version__             = "1.0.0"

__python_version_string = "Python %i.%i.%i-%s" % sys.version_info[:4]

__modules               = []

__items                 = []

__routes                = {}

zabbix_module_path      = "/var/lib/zabbix/modules/python"

item_timeout            = 0

def info(msg):
  if zabbix_runtime:
    zabbix_runtime.log(127, msg)

def trace(msg):
  if zabbix_runtime:
    zabbix_runtime.log(5, msg)

def debug(msg):
  if zabbix_runtime:
    zabbix_runtime.log(4, msg)

def warning(msg):
  if zabbix_runtime:
    zabbix_runtime.log(3, msg)

def error(msg):
  if zabbix_runtime:
    zabbix_runtime.log(2, msg)

def critical(msg):
  if zabbix_runtime:
    zabbix_runtime.log(1, msg)

class AgentRequest:
  key    = None
  params = []

  def __init__(self, key, params):
    self.key = key
    self.params = params

class AgentItem:
  key        = None
  flags      = 0
  test_param = None
  fn         = None

  def __init__(self, key, flags = 0, fn = None, test_param = None):
    if not key:
      raise ValueError("key not given in agent item")

    if not fn:
      raise ValueError("fn not given in agent item")

    # join test_param if list or tuple given
    if test_param:        
      try:
        for i, v in enumerate(test_param):
          test_param[i] = str(v)

        test_param = ",".join(test_param)
      except TypeError:
        test_param = str(test_param)

    self.key = key
    self.flags = flags
    self.test_param = test_param
    self.fn = fn

  def __str__(self):
    return self.key

def route(request):
  """
  Route a request from the Zabbix agent to the Python function associated with
  the request key.
  """

  debug("routing python request: %s" % request.key)

  try:
    return __routes[request.key](request)

  except KeyError:
    raise ValueError("no function registered for agent item " + request.key)

def version(request):
  """Agent item python.version returns the runtime version string"""
  
  return __python_version_string

def register_item(item):
  debug("registering item %s" % item.key)
  __items.append(item)
  __routes[item.key] = item.fn

  return item

def register_module_items(mod):
  if isinstance(mod, str):
    mod = sys.modules[mod]

  debug("calling %s.zbx_module_item_list" % mod)
  try:
    newitems = mod.zbx_module_item_list()

    try:
      for item in newitems:
        register_item(item)
    except TypeError:
      # newitems is probably a single item
      register_item(newitems)

  except AttributeError:
    # module does not define zbx_module_item_list
    newitems = []

  return newitems

def register_module(mod):
  # import module
  debug("registering module: %s" % mod)
  if isinstance(mod, str):
      mod = __import__(mod)

  __modules.append(mod)

  # init module
  try:
    debug("calling %s.zbx_module_init")
    mod.zbx_module_init()
  except AttributeError:
    pass

  # register items
  register_module_items(mod)

  return mod

def zbx_module_init():
  # ensure module path is in search path
  sys.path.insert(0, zabbix_module_path)

  # register builtin items
  register_item(AgentItem("python.version", fn = version))

  # register installed agent modules
  mod_names = []
  for path in glob.glob(zabbix_module_path + "/*.py"):
    filename = os.path.basename(path)
    mod_name = filename[0:len(filename) - 3]

    if mod_name != __name__:
      mod_names.append(filename)
      register_module(mod_name)

  info("loaded python modules: %s" % ", ".join(mod_names))

def zbx_module_item_list():
  return __items
