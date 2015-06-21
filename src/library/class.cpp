/*
Copyright (c) 2014 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <string>
#include "util/lbool.h"
#include "util/sstream.h"
#include "kernel/instantiate.h"
#include "library/scoped_ext.h"
#include "library/kernel_serializer.h"
#include "library/reducible.h"
#include "library/aliases.h"
#include "library/tc_multigraph.h"
#include "library/protected.h"

#ifndef LEAN_INSTANCE_DEFAULT_PRIORITY
#define LEAN_INSTANCE_DEFAULT_PRIORITY 1000
#endif

namespace lean {
enum class class_entry_kind { Class, Multi, Instance, TransInstance, DerivedTransInstance };
struct class_entry {
    class_entry_kind m_kind;
    name             m_class;
    name             m_instance; // only relevant if m_kind == Instance
    unsigned         m_priority; // only relevant if m_kind == Instance
    class_entry():m_kind(class_entry_kind::Class), m_priority(0) {}
    explicit class_entry(name const & c):m_kind(class_entry_kind::Class), m_class(c), m_priority(0) {}
    class_entry(class_entry_kind k, name const & c, name const & i, unsigned p):
        m_kind(k), m_class(c), m_instance(i), m_priority(p) {}
    class_entry(name const & c, bool):
        m_kind(class_entry_kind::Multi), m_class(c) {}
};

struct class_state {
    typedef name_map<list<name>> class_instances;
    typedef name_map<unsigned>   instance_priorities;
    class_instances       m_instances;
    class_instances       m_derived_trans_instances;
    instance_priorities   m_priorities;
    name_set              m_multiple; // set of classes that allow multiple solutions/instances
    tc_multigraph         m_mgraph;

    class_state():m_mgraph("transitive instance") {}

    unsigned get_priority(name const & i) const {
        if (auto it = m_priorities.find(i))
            return *it;
        else
            return LEAN_INSTANCE_DEFAULT_PRIORITY;
    }

    bool is_instance(name const & i) const {
        return m_priorities.contains(i);
    }

    bool try_multiple_instances(name const & c) const {
        return m_multiple.contains(c);
    }

    list<name> insert(name const & inst, unsigned priority, list<name> const & insts) const {
        if (!insts)
            return to_list(inst);
        else if (priority >= get_priority(head(insts)))
            return list<name>(inst, insts);
        else
            return list<name>(head(insts), insert(inst, priority, tail(insts)));
    }

    void add_class(name const & c) {
        auto it = m_instances.find(c);
        if (!it)
            m_instances.insert(c, list<name>());
    }

    void add_instance(name const & c, name const & i, unsigned p) {
        auto it = m_instances.find(c);
        if (!it) {
            m_instances.insert(c, to_list(i));
        } else {
            auto lst = filter(*it, [&](name const & i1) { return i1 != i; });
            m_instances.insert(c, insert(i, p, lst));
        }
        m_priorities.insert(i, p);
    }

    void add_trans_instance(environment const & env, name const & c, name const & i, unsigned p) {
        add_instance(c, i, p);
        m_mgraph.add1(env, i);
    }

    void add_derived_trans_instance(environment const & env, name const & c, name const & i) {
        auto it = m_derived_trans_instances.find(c);
        if (!it) {
            m_derived_trans_instances.insert(c, to_list(i));
        } else {
            auto lst = filter(*it, [&](name const & i1) { return i1 != i; });
            m_derived_trans_instances.insert(c, cons(i, lst));
        }
        m_mgraph.add1(env, i);
    }

    void add_multiple(name const & c) {
        add_class(c);
        m_multiple.insert(c);
    }
};

static name * g_class_name = nullptr;
static std::string * g_key = nullptr;

struct class_config {
    typedef class_state state;
    typedef class_entry entry;
    static void add_entry(environment const & env, io_state const &, state & s, entry const & e) {
        switch (e.m_kind) {
        case class_entry_kind::Class:
            s.add_class(e.m_class);
            break;
        case class_entry_kind::Multi:
            s.add_multiple(e.m_class);
            break;
        case class_entry_kind::Instance:
            s.add_instance(e.m_class, e.m_instance, e.m_priority);
            break;
        case class_entry_kind::TransInstance:
            s.add_trans_instance(env, e.m_class, e.m_instance, e.m_priority);
            break;
        case class_entry_kind::DerivedTransInstance:
            s.add_derived_trans_instance(env, e.m_class, e.m_instance);
            break;
        }
    }
    static name const & get_class_name() {
        return *g_class_name;
    }
    static std::string const & get_serialization_key() {
        return *g_key;
    }
    static void  write_entry(serializer & s, entry const & e) {
        s << static_cast<char>(e.m_kind);
        switch (e.m_kind) {
        case class_entry_kind::Class:
        case class_entry_kind::Multi:
            s << e.m_class;
            break;
        case class_entry_kind::Instance:
        case class_entry_kind::TransInstance:
            s << e.m_class << e.m_instance << e.m_priority;
            break;
        case class_entry_kind::DerivedTransInstance:
            s << e.m_class << e.m_instance;
            break;
        }
    }
    static entry read_entry(deserializer & d) {
        entry e; char k;
        d >> k;
        e.m_kind = static_cast<class_entry_kind>(k);
        switch (e.m_kind) {
        case class_entry_kind::Class:
        case class_entry_kind::Multi:
            d >> e.m_class;
            break;
        case class_entry_kind::Instance:
        case class_entry_kind::TransInstance:
            d >> e.m_class >> e.m_instance >> e.m_priority;
            break;
        case class_entry_kind::DerivedTransInstance:
            d >> e.m_class >> e.m_instance;
            break;
        }
        return e;
    }
    static optional<unsigned> get_fingerprint(entry const & e) {
        switch (e.m_kind) {
        case class_entry_kind::Class:
        case class_entry_kind::Multi:
            return some(e.m_class.hash());
        case class_entry_kind::Instance:
        case class_entry_kind::TransInstance:
            return some(hash(hash(e.m_class.hash(), e.m_instance.hash()), e.m_priority));
        case class_entry_kind::DerivedTransInstance:
            return some(hash(e.m_class.hash(), e.m_instance.hash()));
        }
        lean_unreachable();
    }
};

template class scoped_ext<class_config>;
typedef scoped_ext<class_config> class_ext;

static void check_class(environment const & env, name const & c_name) {
    env.get(c_name);
}

static void check_is_class(environment const & env, name const & c_name) {
    class_state const & s = class_ext::get_state(env);
    if (!s.m_instances.contains(c_name))
        throw exception(sstream() << "'" << c_name << "' is not a class");
}

name get_class_name(environment const & env, expr const & e) {
    if (!is_constant(e))
        throw exception("class expected, expression is not a constant");
    name const & c_name = const_name(e);
    check_is_class(env, c_name);
    return c_name;
}

environment add_class(environment const & env, name const & n, bool persistent) {
    check_class(env, n);
    return class_ext::add_entry(env, get_dummy_ios(), class_entry(n), persistent);
}

void get_classes(environment const & env, buffer<name> & classes) {
    class_state const & s = class_ext::get_state(env);
    s.m_instances.for_each([&](name const & c, list<name> const &) {
            classes.push_back(c);
        });
}

bool is_class(environment const & env, name const & c) {
    class_state const & s = class_ext::get_state(env);
    return s.m_instances.contains(c);
}

type_checker_ptr mk_class_type_checker(environment const & env, name_generator && ngen, bool conservative) {
    auto pred = conservative ? mk_not_reducible_pred(env) : mk_irreducible_pred(env);
    class_state s = class_ext::get_state(env);
    return mk_type_checker(env, std::move(ngen), [=](name const & n) {
            return s.m_instances.contains(n) || pred(n);
        });
}

static name * g_tmp_prefix = nullptr;
static environment add_instance_core(environment const & env, class_entry_kind k, name const & n, unsigned priority, bool persistent) {
    declaration d = env.get(n);
    expr type = d.get_type();
    name_generator ngen(*g_tmp_prefix);
    auto tc = mk_class_type_checker(env, ngen.mk_child(), false);
    while (true) {
        type = tc->whnf(type).first;
        if (!is_pi(type))
            break;
        type = instantiate(binding_body(type), mk_local(ngen.next(), binding_domain(type)));
    }
    name c = get_class_name(env, get_app_fn(type));
    check_is_class(env, c);
    return class_ext::add_entry(env, get_dummy_ios(), class_entry(k, c, n, priority), persistent);
}

environment add_instance(environment const & env, name const & n, unsigned priority, bool persistent) {
    return add_instance_core(env, class_entry_kind::Instance, n, priority, persistent);
}

environment add_instance(environment const & env, name const & n, bool persistent) {
    return add_instance(env, n, LEAN_INSTANCE_DEFAULT_PRIORITY, persistent);
}

environment add_trans_instance(environment const & env, name const & n, unsigned priority, bool persistent) {
    class_state const & s = class_ext::get_state(env);
    tc_multigraph g    = s.m_mgraph;
    pair<environment, list<name>> new_env_insts = g.add(env, n);
    environment new_env = new_env_insts.first;
    new_env = add_instance_core(new_env, class_entry_kind::TransInstance, n, priority, persistent);
    for (name const & tn : new_env_insts.second) {
        new_env = add_instance_core(new_env, class_entry_kind::DerivedTransInstance, tn, 0, persistent);
        new_env = set_reducible(new_env, tn, reducible_status::Reducible, persistent);
        new_env = add_protected(new_env, tn);
    }
    return new_env;
}

environment add_trans_instance(environment const & env, name const & n, bool persistent) {
    return add_trans_instance(env, n, LEAN_INSTANCE_DEFAULT_PRIORITY, persistent);
}

environment mark_multiple_instances(environment const & env, name const & n, bool persistent) {
    check_class(env, n);
    return class_ext::add_entry(env, get_dummy_ios(), class_entry(n, true), persistent);
}

bool try_multiple_instances(environment const & env, name const & n) {
    class_state const & s = class_ext::get_state(env);
    return s.try_multiple_instances(n);
}

bool is_instance(environment const & env, name const & i) {
    class_state const & s = class_ext::get_state(env);
    return s.is_instance(i);
}

list<name> get_class_instances(environment const & env, name const & c) {
    class_state const & s = class_ext::get_state(env);
    return ptr_to_list(s.m_instances.find(c));
}

list<name> get_class_derived_trans_instances(environment const & env, name const & c) {
    class_state const & s = class_ext::get_state(env);
    return ptr_to_list(s.m_derived_trans_instances.find(c));
}

/** \brief If the constant \c e is a class, return its name */
static optional<name> constant_is_ext_class(environment const & env, expr const & e) {
    name const & cls_name = const_name(e);
    if (is_class(env, cls_name)) {
        return optional<name>(cls_name);
    } else {
        return optional<name>();
    }
}

/** \brief Partial/Quick test for is_ext_class. Result
    l_true:   \c type is a class, and the name of the class is stored in \c result.
    l_false:  \c type is not a class.
    l_undef:  procedure did not establish whether \c type is a class or not.
*/
static lbool is_quick_ext_class(type_checker const & tc, expr const & type, name & result) {
    environment const & env = tc.env();
    expr const * it         = &type;
    while (true) {
        switch (it->kind()) {
        case expr_kind::Var:  case expr_kind::Sort:   case expr_kind::Local:
        case expr_kind::Meta: case expr_kind::Lambda:
            return l_false;
        case expr_kind::Macro:
            return l_undef;
        case expr_kind::Constant:
            if (auto r = constant_is_ext_class(env, *it)) {
                result = *r;
                return l_true;
            } else if (tc.is_opaque(*it)) {
                return l_false;
            } else {
                return l_undef;
            }
        case expr_kind::App: {
            expr const & f = get_app_fn(*it);
            if (is_constant(f)) {
                if (auto r = constant_is_ext_class(env, f)) {
                    result = *r;
                    return l_true;
                } else if (tc.is_opaque(f)) {
                    return l_false;
                } else {
                    return l_undef;
                }
            } else if (is_lambda(f) || is_macro(f)) {
                return l_undef;
            } else {
                return l_false;
            }
        }
        case expr_kind::Pi:
            it = &binding_body(*it);
            break;
        }
    }
}

/** \brief Full/Expensive test for \c is_ext_class */
static optional<name> is_full_ext_class(type_checker & tc, expr type) {
    type = tc.whnf(type).first;
    if (is_pi(type)) {
        return is_full_ext_class(tc, instantiate(binding_body(type), mk_local(tc.mk_fresh_name(), binding_domain(type))));
    } else {
        expr f = get_app_fn(type);
        if (!is_constant(f))
            return optional<name>();
        return constant_is_ext_class(tc.env(), f);
    }
}

/** \brief Return true iff \c type is a class or Pi that produces a class. */
optional<name> is_ext_class(type_checker & tc, expr const & type) {
    name result;
    switch (is_quick_ext_class(tc, type, result)) {
    case l_true:  return optional<name>(result);
    case l_false: return optional<name>();
    case l_undef: break;
    }
    return is_full_ext_class(tc, type);
}

/** \brief Return a list of instances of the class \c cls_name that occur in \c ctx */
list<expr> get_local_instances(type_checker & tc, list<expr> const & ctx, name const & cls_name) {
    buffer<expr> buffer;
    for (auto const & l : ctx) {
        if (!is_local(l))
            continue;
        expr inst_type    = mlocal_type(l);
        if (auto it = is_ext_class(tc, inst_type))
            if (*it == cls_name)
                buffer.push_back(l);
    }
    return to_list(buffer.begin(), buffer.end());
}

void initialize_class() {
    g_tmp_prefix = new name(name::mk_internal_unique_name());
    g_class_name = new name("classes");
    g_key = new std::string("class");
    class_ext::initialize();
}

void finalize_class() {
    class_ext::finalize();
    delete g_key;
    delete g_class_name;
    delete g_tmp_prefix;
}
}
