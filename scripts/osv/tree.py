import sys
from operator import attrgetter

class TreeNode(object):
    def __init__(self, key):
        self.key = key
        self.children_by_key = {}

    def get_or_add(self, key):
        node = self.children_by_key.get(key, None)
        if not node:
            node = self.__class__(key)
            self.add(node)
        return node

    def add(self, node):
        self.children_by_key[node.key] = node

    def squash_child(self):
        assert self.has_only_one_child()
        self.children_by_key = next(self.children_by_key.itervalues()).children_by_key

    @property
    def children(self):
        return self.children_by_key.itervalues()

    def has_only_one_child(self):
        return len(self.children_by_key) == 1

    def has_children(self):
        return bool(self.children_by_key)

    def remove_all(self):
        self.children_by_key.clear()

def print_tree(root_node,
        formatter=attrgetter('key'),
        order_by=attrgetter('key'),
        printer=sys.stdout.write,
        node_filter=None,
        collapse_similar=True):

    def print_node(node, is_last_history):
        stems = (" |   ", "     ")
        branches = (" |-- ", " \-- ")

        label_lines = formatter(node).split('\n')
        prefix_without_branch = ''.join(map(stems.__getitem__, is_last_history[:-1]))

        if is_last_history:
            printer(prefix_without_branch)
            printer(branches[is_last_history[-1]])
        printer("%s\n" % label_lines[0])

        for line in label_lines[1:]:
            printer(''.join(map(stems.__getitem__, is_last_history)))
            printer("%s\n" % line)

        children = sorted(filter(node_filter, node.children), key=order_by)
        if children:
            for child in children[:-1]:
                print_node(child, is_last_history + [False])
            print_node(children[-1], is_last_history + [True])

        is_last = not is_last_history or is_last_history[-1]
        if not is_last:
           printer("%s%s\n" % (prefix_without_branch, stems[False]))

    if not node_filter or node_filter(root_node):
        print_node(root_node, [])
