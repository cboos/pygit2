/*
 * Copyright 2010-2012 The pygit2 contributors
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2,
 * as published by the Free Software Foundation.
 *
 * In addition to the permissions in the GNU General Public License,
 * the authors give you unlimited permission to link the compiled
 * version of this file into combinations with other programs,
 * and to distribute those combinations without any restriction
 * coming from the use of this file.  (The General Public License
 * restrictions do apply in other respects; for example, they cover
 * modification of the file, and distribution when not linked into
 * a combined executable.)
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <pygit2/error.h>
#include <pygit2/types.h>
#include <pygit2/utils.h>
#include <pygit2/oid.h>
#include <pygit2/index.h>

extern PyTypeObject IndexType;
extern PyTypeObject TreeType;
extern PyTypeObject DiffType;
extern PyTypeObject IndexIterType;
extern PyTypeObject IndexEntryType;

int
Index_init(Index *self, PyObject *args, PyObject *kwds)
{
    char *path;
    int err;

    if (kwds) {
        PyErr_SetString(PyExc_TypeError, "Index takes no keyword arguments");
        return -1;
    }

    if (!PyArg_ParseTuple(args, "s", &path))
        return -1;

    err = git_index_open(&self->index, path);
    if (err < 0) {
        Error_set_str(err, path);
        return -1;
    }

    return 0;
}

void
Index_dealloc(Index* self)
{
    PyObject_GC_UnTrack(self);
    Py_XDECREF(self->repo);
    git_index_free(self->index);
    PyObject_GC_Del(self);
}

int
Index_traverse(Index *self, visitproc visit, void *arg)
{
    Py_VISIT(self->repo);
    return 0;
}


PyDoc_STRVAR(Index_add__doc__,
  "add(path)\n"
  "\n"
  "Add or update an index entry from a file in disk.");

PyObject *
Index_add(Index *self, PyObject *args)
{
    int err;
    const char *path;

    if (!PyArg_ParseTuple(args, "s", &path))
        return NULL;

    err = git_index_add_bypath(self->index, path);
    if (err < 0)
        return Error_set_str(err, path);

    Py_RETURN_NONE;
}


PyDoc_STRVAR(Index_clear__doc__,
  "clear()\n"
  "\n"
  "Clear the contents (all the entries) of an index object.");

PyObject *
Index_clear(Index *self)
{
    git_index_clear(self->index);
    Py_RETURN_NONE;
}


PyDoc_STRVAR(Index_diff__doc__,
  "diff([tree]) -> Diff\n"
  "\n"
  "Return a :py:class:`~pygit2.Diff` object with the differences between the\n"
  "index and the working copy. If a :py:class:`~pygit2.Tree` object is\n"
  "passed, return the diferences between the index and the given tree.");

PyObject *
Index_diff(Index *self, PyObject *args)
{
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    git_diff_list *diff;
    int err;

    Diff *py_diff;
    PyObject *py_obj = NULL;

    if (!PyArg_ParseTuple(args, "|O", &py_obj))
        return NULL;

    if (py_obj == NULL) {
        err = git_diff_index_to_workdir(
                &diff,
                self->repo->repo,
                self->index,
                &opts);
    } else if (PyObject_TypeCheck(py_obj, &TreeType)) {
        err = git_diff_tree_to_index(
                &diff,
                self->repo->repo,
                ((Tree *)py_obj)->tree,
                self->index,
                &opts);
    } else {
        PyErr_SetObject(PyExc_TypeError, py_obj);
        return NULL;
    }
    if (err < 0)
        return Error_set(err);

    py_diff = PyObject_New(Diff, &DiffType);
    if (py_diff) {
        Py_INCREF(self->repo);
        py_diff->repo = self->repo;
        py_diff->diff = diff;
    }

    return (PyObject*)py_diff;
}


PyDoc_STRVAR(Index__find__doc__,
  "_find(path) -> integer\n"
  "\n"
  "Find the first index of any entries which point to given path in the\n"
  "index file.");

PyObject *
Index__find(Index *self, PyObject *py_path)
{
    char *path;
    size_t idx;
    int err;

    path = PyString_AsString(py_path);
    if (!path)
        return NULL;

    err = git_index_find(&idx, self->index, path);
    if (err < 0)
        return Error_set_str(err, path);

    return PyLong_FromSize_t(idx);
}


PyDoc_STRVAR(Index_read__doc__,
  "read()\n"
  "\n"
  "Update the contents of an existing index object in memory by reading from\n"
  "the hard disk.");

PyObject *
Index_read(Index *self)
{
    int err;

    err = git_index_read(self->index);
    if (err < GIT_OK)
        return Error_set(err);

    Py_RETURN_NONE;
}


PyDoc_STRVAR(Index_write__doc__,
  "write()\n"
  "\n"
  "Write an existing index object from memory back to disk using an atomic\n"
  "file lock.");

PyObject *
Index_write(Index *self)
{
    int err;

    err = git_index_write(self->index);
    if (err < GIT_OK)
        return Error_set(err);

    Py_RETURN_NONE;
}

/* This is an internal function, used by Index_getitem and Index_setitem */
size_t
Index_get_position(Index *self, PyObject *value)
{
    char *path;
    size_t idx;
    int err;

    /* Case 1: integer */
    if (PyInt_Check(value)) {
        err = (int)PyInt_AsLong(value);
        if (err == -1 && PyErr_Occurred())
            return -1;
        if (err < 0) {
            PyErr_SetObject(PyExc_ValueError, value);
            return -1;
        }
        return err;
    }

    /* Case 2: byte or text string */
    path = py_path_to_c_str(value);
    if (!path)
        return -1;

    err = git_index_find(&idx, self->index, path);
    if (err < 0) {
        Error_set_str(err, path);
        free(path);
        return -1;
    }

    free(path);

    return idx;
}

int
Index_contains(Index *self, PyObject *value)
{
    char *path;
    int err;

    path = py_path_to_c_str(value);
    if (!path)
        return -1;
    err = git_index_find(NULL, self->index, path);
    if (err == GIT_ENOTFOUND) {
        free(path);
        return 0;
    }
    if (err < 0) {
        Error_set_str(err, path);
        free(path);
        return -1;
    }
    free(path);
    return 1;
}

PyObject *
Index_iter(Index *self)
{
    IndexIter *iter;

    iter = PyObject_New(IndexIter, &IndexIterType);
    if (iter) {
        Py_INCREF(self);
        iter->owner = self;
        iter->i = 0;
    }
    return (PyObject*)iter;
}

Py_ssize_t
Index_len(Index *self)
{
    return (Py_ssize_t)git_index_entrycount(self->index);
}

PyObject *
wrap_index_entry(const git_index_entry *entry, Index *index)
{
    IndexEntry *py_entry;

    py_entry = PyObject_New(IndexEntry, &IndexEntryType);
    if (py_entry)
        py_entry->entry = entry;

    return (PyObject*)py_entry;
}

PyObject *
Index_getitem(Index *self, PyObject *value)
{
    size_t idx;
    const git_index_entry *index_entry;

    idx = Index_get_position(self, value);
    if (idx == -1)
        return NULL;

    index_entry = git_index_get_byindex(self->index, idx);
    if (!index_entry) {
        PyErr_SetObject(PyExc_KeyError, value);
        return NULL;
    }

    return wrap_index_entry(index_entry, self);
}


PyDoc_STRVAR(Index_remove__doc__,
  "remove(path)\n"
  "\n"
  "Removes an entry from index.");

PyObject *
Index_remove(Index *self, PyObject *args)
{
    int err;
    const char *path;

    if (!PyArg_ParseTuple(args, "s", &path))
        return NULL;

    err = git_index_remove(self->index, path, 0);
    if (err < 0) {
        Error_set(err);
        return NULL;
    }

    Py_RETURN_NONE;
}

int
Index_setitem(Index *self, PyObject *key, PyObject *value)
{
    if (value != NULL) {
        PyErr_SetString(PyExc_NotImplementedError,
                        "set item on index not yet implemented");
        return -1;
    }

    if(Index_remove(self, Py_BuildValue("(N)", key)) == NULL)
      return -1;

    return 0;
}


PyDoc_STRVAR(Index_read_tree__doc__,
  "read_tree(tree)\n"
  "\n"
  "Update the index file from the tree identified by the given oid.");

PyObject *
Index_read_tree(Index *self, PyObject *value)
{
    git_oid oid;
    git_tree *tree;
    int err, len;

    len = py_str_to_git_oid(value, &oid);
    if (len < 0)
        return NULL;

    err = git_tree_lookup_prefix(&tree, self->repo->repo, &oid,
                                 (unsigned int)len);
    if (err < 0)
        return Error_set(err);

    err = git_index_read_tree(self->index, tree);
    git_tree_free(tree);
    if (err < 0)
        return Error_set(err);

    Py_RETURN_NONE;
}


PyDoc_STRVAR(Index_write_tree__doc__,
  "write_tree() -> str\n"
  "\n"
  "Create a tree object from the index file, return its oid.");

PyObject *
Index_write_tree(Index *self)
{
    git_oid oid;
    int err;

    err = git_index_write_tree(&oid, self->index);
    if (err < 0)
        return Error_set(err);

    return git_oid_to_python(oid.id);
}

PyMethodDef Index_methods[] = {
    METHOD(Index, add, METH_VARARGS),
    METHOD(Index, remove, METH_VARARGS),
    METHOD(Index, clear, METH_NOARGS),
    METHOD(Index, diff, METH_VARARGS),
    METHOD(Index, _find, METH_O),
    METHOD(Index, read, METH_NOARGS),
    METHOD(Index, write, METH_NOARGS),
    METHOD(Index, read_tree, METH_O),
    METHOD(Index, write_tree, METH_NOARGS),
    {NULL}
};

PySequenceMethods Index_as_sequence = {
    0,                          /* sq_length */
    0,                          /* sq_concat */
    0,                          /* sq_repeat */
    0,                          /* sq_item */
    0,                          /* sq_slice */
    0,                          /* sq_ass_item */
    0,                          /* sq_ass_slice */
    (objobjproc)Index_contains, /* sq_contains */
};

PyMappingMethods Index_as_mapping = {
    (lenfunc)Index_len,              /* mp_length */
    (binaryfunc)Index_getitem,       /* mp_subscript */
    (objobjargproc)Index_setitem,    /* mp_ass_subscript */
};

PyDoc_STRVAR(Index__doc__, "Index file.");

PyTypeObject IndexType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_pygit2.Index",                            /* tp_name           */
    sizeof(Index),                             /* tp_basicsize      */
    0,                                         /* tp_itemsize       */
    (destructor)Index_dealloc,                 /* tp_dealloc        */
    0,                                         /* tp_print          */
    0,                                         /* tp_getattr        */
    0,                                         /* tp_setattr        */
    0,                                         /* tp_compare        */
    0,                                         /* tp_repr           */
    0,                                         /* tp_as_number      */
    &Index_as_sequence,                        /* tp_as_sequence    */
    &Index_as_mapping,                         /* tp_as_mapping     */
    0,                                         /* tp_hash           */
    0,                                         /* tp_call           */
    0,                                         /* tp_str            */
    0,                                         /* tp_getattro       */
    0,                                         /* tp_setattro       */
    0,                                         /* tp_as_buffer      */
    Py_TPFLAGS_DEFAULT |
    Py_TPFLAGS_BASETYPE |
    Py_TPFLAGS_HAVE_GC,                        /* tp_flags          */
    Index__doc__,                              /* tp_doc            */
    (traverseproc)Index_traverse,              /* tp_traverse       */
    0,                                         /* tp_clear          */
    0,                                         /* tp_richcompare    */
    0,                                         /* tp_weaklistoffset */
    (getiterfunc)Index_iter,                   /* tp_iter           */
    0,                                         /* tp_iternext       */
    Index_methods,                             /* tp_methods        */
    0,                                         /* tp_members        */
    0,                                         /* tp_getset         */
    0,                                         /* tp_base           */
    0,                                         /* tp_dict           */
    0,                                         /* tp_descr_get      */
    0,                                         /* tp_descr_set      */
    0,                                         /* tp_dictoffset     */
    (initproc)Index_init,                      /* tp_init           */
    0,                                         /* tp_alloc          */
    0,                                         /* tp_new            */
};


void
IndexIter_dealloc(IndexIter *self)
{
    Py_CLEAR(self->owner);
    PyObject_Del(self);
}

PyObject *
IndexIter_iternext(IndexIter *self)
{
    const git_index_entry *index_entry;

    index_entry = git_index_get_byindex(self->owner->index, self->i);
    if (!index_entry)
        return NULL;

    self->i += 1;
    return wrap_index_entry(index_entry, self->owner);
}


PyDoc_STRVAR(IndexIter__doc__, "Index iterator.");

PyTypeObject IndexIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_pygit2.IndexIter",                       /* tp_name           */
    sizeof(IndexIter),                         /* tp_basicsize      */
    0,                                         /* tp_itemsize       */
    (destructor)IndexIter_dealloc ,            /* tp_dealloc        */
    0,                                         /* tp_print          */
    0,                                         /* tp_getattr        */
    0,                                         /* tp_setattr        */
    0,                                         /* tp_compare        */
    0,                                         /* tp_repr           */
    0,                                         /* tp_as_number      */
    0,                                         /* tp_as_sequence    */
    0,                                         /* tp_as_mapping     */
    0,                                         /* tp_hash           */
    0,                                         /* tp_call           */
    0,                                         /* tp_str            */
    PyObject_GenericGetAttr,                   /* tp_getattro       */
    0,                                         /* tp_setattro       */
    0,                                         /* tp_as_buffer      */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,  /* tp_flags          */
    IndexIter__doc__,                          /* tp_doc            */
    0,                                         /* tp_traverse       */
    0,                                         /* tp_clear          */
    0,                                         /* tp_richcompare    */
    0,                                         /* tp_weaklistoffset */
    PyObject_SelfIter,                         /* tp_iter           */
    (iternextfunc)IndexIter_iternext,          /* tp_iternext       */
};

void
IndexEntry_dealloc(IndexEntry *self)
{
    PyObject_Del(self);
}


PyDoc_STRVAR(IndexEntry_mode__doc__, "Mode.");

PyObject *
IndexEntry_mode__get__(IndexEntry *self)
{
    return PyInt_FromLong(self->entry->mode);
}


PyDoc_STRVAR(IndexEntry_path__doc__, "Path.");

PyObject *
IndexEntry_path__get__(IndexEntry *self)
{
    return to_path(self->entry->path);
}


PyDoc_STRVAR(IndexEntry_oid__doc__, "Object id.");

PyObject *
IndexEntry_oid__get__(IndexEntry *self)
{
    return git_oid_to_python(self->entry->oid.id);
}


PyDoc_STRVAR(IndexEntry_hex__doc__, "Hex id.");

PyObject *
IndexEntry_hex__get__(IndexEntry *self)
{
    return git_oid_to_py_str(&self->entry->oid);
}

PyGetSetDef IndexEntry_getseters[] = {
    GETTER(IndexEntry, mode),
    GETTER(IndexEntry, path),
    GETTER(IndexEntry, oid),
    GETTER(IndexEntry, hex),
    {NULL},
};

PyDoc_STRVAR(IndexEntry__doc__, "Index entry.");

PyTypeObject IndexEntryType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_pygit2.IndexEntry",                       /* tp_name           */
    sizeof(IndexEntry),                        /* tp_basicsize      */
    0,                                         /* tp_itemsize       */
    (destructor)IndexEntry_dealloc,            /* tp_dealloc        */
    0,                                         /* tp_print          */
    0,                                         /* tp_getattr        */
    0,                                         /* tp_setattr        */
    0,                                         /* tp_compare        */
    0,                                         /* tp_repr           */
    0,                                         /* tp_as_number      */
    0,                                         /* tp_as_sequence    */
    0,                                         /* tp_as_mapping     */
    0,                                         /* tp_hash           */
    0,                                         /* tp_call           */
    0,                                         /* tp_str            */
    0,                                         /* tp_getattro       */
    0,                                         /* tp_setattro       */
    0,                                         /* tp_as_buffer      */
    Py_TPFLAGS_DEFAULT,                        /* tp_flags          */
    IndexEntry__doc__,                         /* tp_doc            */
    0,                                         /* tp_traverse       */
    0,                                         /* tp_clear          */
    0,                                         /* tp_richcompare    */
    0,                                         /* tp_weaklistoffset */
    0,                                         /* tp_iter           */
    0,                                         /* tp_iternext       */
    0,                                         /* tp_methods        */
    0,                                         /* tp_members        */
    IndexEntry_getseters,                      /* tp_getset         */
    0,                                         /* tp_base           */
    0,                                         /* tp_dict           */
    0,                                         /* tp_descr_get      */
    0,                                         /* tp_descr_set      */
    0,                                         /* tp_dictoffset     */
    0,                                         /* tp_init           */
    0,                                         /* tp_alloc          */
    0,                                         /* tp_new            */
};
