import gdb
import gdb.printing

class ValuePrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        kind = int(self.val['kind'])
        
        # VAL_INT
        if kind == 0:               
            n = int(self.val['as_int'])
            return f"Value(int, {n})"
        
        # VAL_FLOAT
        elif kind == 1:             
            d = float(self.val['as_float'])
            return f"Value(float, {d:.5g})"
        
        # VAL_STRING
        elif kind == 2:             
            s   = self.val['as_str']
            length = int(s['len'])
            text = s['ptr'].string(length=length)
            return f'Value(str, "{text}")'
        
        # Not supported in printer
        return f"Value(UNKNOWN kind={kind})"

def build_pp():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("my_app")
    # pp.add_printer(display_name, type_name_regex, printer_class)
    pp.add_printer("Value", r"^Value$", ValuePrinter)
    return pp

gdb.printing.register_pretty_printer(gdb.current_objfile(), build_pp())

