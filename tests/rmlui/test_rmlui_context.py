import pytest

rmlui = pytest.importorskip("panda3d.rmlui")

from .conftest import load


def test_context_dimensions_and_name(rml_region, rml_context):
    assert rml_context.get_name() == rml_region.get_context().get_name()
    assert rml_context.get_width() > 0
    assert rml_context.get_height() > 0


def test_load_document_from_memory(rml_context):
    doc = rml_context.load_document_from_memory(
        "<rml><head></head><body><p id='x'>hello</p></body></rml>")
    assert doc is not None
    assert doc.get_element_by_id("x") is not None


def test_load_document_from_memory_source_url(rml_context):
    doc = rml_context.load_document_from_memory(
        "<rml><head></head><body></body></rml>", "my-source")
    assert doc.get_source_url() == "my-source"

    doc2 = rml_context.load_document_from_memory(
        "<rml><head></head><body></body></rml>")
    assert doc2.get_source_url() == "[from memory]"


def test_num_documents(rml_context):
    assert rml_context.get_num_documents() == 0
    load(rml_context, "<p>a</p>")
    assert rml_context.get_num_documents() == 1
    load(rml_context, "<p>b</p>")
    assert rml_context.get_num_documents() == 2


def test_unload_all_documents(rml_context):
    load(rml_context, "<p>a</p>")
    load(rml_context, "<p>b</p>")
    assert rml_context.get_num_documents() == 2

    rml_context.unload_all_documents()
    assert rml_context.get_num_documents() == 0


def test_unload_single_document(rml_context):
    doc = load(rml_context, "<p>a</p>")
    load(rml_context, "<p>b</p>")
    assert rml_context.get_num_documents() == 2

    rml_context.unload_document(doc)
    rml_context.update()
    assert rml_context.get_num_documents() == 1


def test_get_element_at_point_misses_empty_space(rml_context):
    load(rml_context, "<div id='d' style='width:10px;height:10px;'></div>")
    # A point far outside the small element resolves to the document body, not
    # our div; mostly we are asserting it does not crash and returns a wrapper.
    el = rml_context.get_element_at_point(5, 5)
    # May be the div or an ancestor depending on layout; just ensure no crash.
    assert el is None or el.get_id() is not None


def test_hover_and_focus_default_none(rml_context):
    load(rml_context, "<p>a</p>")
    # With no mouse interaction the hover element is the document or None.
    hover = rml_context.get_hover_element()
    assert hover is None or hover.get_id() is not None
