// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview This is a table column representation
 */

cr.define('cr.ui.table', function() {
  /** @const */ var EventTarget = cr.EventTarget;

  /**
   * A table column that wraps column ids and settings.
   * @param {string} id
   * @param {string} name
   * @param {number} width
   * @param {boolean=} opt_endAlign
   * @constructor
   * @extends {cr.EventTarget}
   */
  function TableColumn(id, name, width, opt_endAlign) {
    this.id_ = id;
    this.name_ = name;
    this.width_ = width;
    this.endAlign_ = !!opt_endAlign;
    this.visible_ = true;
  }

  TableColumn.prototype = {
    __proto__: EventTarget.prototype,

    defaultOrder_: 'asc',

    /**
     * Clones column.
     * @return {cr.ui.table.TableColumn} Clone of the given column.
     */
    clone: function() {
      var tableColumn =
          new TableColumn(this.id_, this.name_, this.width_, this.endAlign_);
      tableColumn.renderFunction = this.renderFunction_;
      tableColumn.headerRenderFunction = this.headerRenderFunction_;
      tableColumn.defaultOrder = this.defaultOrder_;

      tableColumn.visible_ = this.visible_;

      return tableColumn;
    },

    /**
     * Renders table cell. This is the default render function.
     * @param {*} dataItem The data item to be rendered.
     * @param {string} columnId The column id.
     * @param {cr.ui.Table} table The table.
     * @return {HTMLElement} Rendered element.
     */
    renderFunction_: function(dataItem, columnId, table) {
      var div = /** @type {HTMLElement} */
          (table.ownerDocument.createElement('div'));
      div.textContent = dataItem[columnId];
      div.hidden = !this.visible;
      return div;
    },

    /**
     * Renders table header. This is the default render function.
     * @param {cr.ui.Table} table The table.
     * @return {Text} Rendered text node.
     */
    headerRenderFunction_: function(table) {
      return table.ownerDocument.createTextNode(this.name);
    },

    /**
     * The width of the column.  Hidden columns have zero width.
     * @type {number}
     */
    get width() {
      return this.visible_ ? this.width_ : 0;
    },

    /**
     * The width of the column, disregarding visibility.  For hidden columns,
     * this would be the width of the column if it were to be made visible.
     * @type {number}
     */
    get absoluteWidth() {
      return this.width_;
    },
  };

  /**
   * The column id.
   * @type {string}
   */
  cr.defineProperty(TableColumn, 'id');

  /**
   * The column name
   * @type {string}
   */
  cr.defineProperty(TableColumn, 'name');

  /**
   * The column width.
   * @type {number}
   */
  cr.defineProperty(TableColumn, 'width');

  /**
   * The column visibility.
   * @type {boolean}
   */
  cr.defineProperty(TableColumn, 'visible');

  /**
   * True if the column is aligned to end.
   * @type {boolean}
   */
  cr.defineProperty(TableColumn, 'endAlign');

  /**
   * The column render function.
   * @type {function(*, string, cr.ui.Table): HTMLElement}
   */
  cr.defineProperty(TableColumn, 'renderFunction');

  /**
   * The column header render function.
   * @type {function(cr.ui.Table): Text}
   */
  cr.defineProperty(TableColumn, 'headerRenderFunction');

  /**
   * Default sorting order for the column ('asc' or 'desc').
   * @type {string}
   */
  cr.defineProperty(TableColumn, 'defaultOrder');

  return {TableColumn: TableColumn};
});
