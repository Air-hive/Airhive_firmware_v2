from zeroconf import ServiceBrowser, Zeroconf


def resolve_service(zeroconf_inst, service_type, name):
    try:
        info = zeroconf_inst.get_service_info(service_type, name)
        addresses = info.parsed_addresses()
        print(f"\tHostname: {info.server}")
        print(f"\tAddress(es): {addresses}")
        print(f"\tPort: {info.port}")
        print(f"\tProperties: {info.properties}")
    except:
        print(f"Could not resolve service {name}.")
        return

class MyServiceListener:
    def add_service(self, zeroconf_inst, service_type, name):
        if name.startswith('Airhive'):
            print(f"Service added: {name} of type {service_type}")
            resolve_service(zeroconf_inst, service_type, name)

    def remove_service(self, zeroconf_inst, service_type, name):
        if name.startswith('Airhive'):
            print(f"Service removed: {name} of type {service_type}")

    def update_service(self, zeroconf_inst, service_type, name):
        if name.startswith('Airhive'):
            print(f"Service updated: {name} of type {service_type}")
            resolve_service(zeroconf_inst, service_type, name)


zeroconf = Zeroconf()
listener = MyServiceListener()
service_browser = ServiceBrowser(zeroconf, "_http._tcp.local.", listener)

try:
    input("Press Enter to exit...\n")
finally:
    zeroconf.close()