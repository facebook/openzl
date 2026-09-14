// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * An insert-only journal that maintains insertion order while providing O(1) lookups.
 * Uses a Set for fast duplicate detection and an array to preserve insertion order.
 */
export class InsertOnlyJournal<T> {
  private readonly backingSet: Set<T>;
  private readonly insertionOrder: T[];

  constructor() {
    this.backingSet = new Set<T>();
    this.insertionOrder = [];
  }

  /**
   * Attempts to insert an item into the journal.
   * @param item The item to insert
   * @returns true if the item was successfully inserted (wasn't already present), false otherwise
   */
  insert(item: T): boolean {
    if (this.backingSet.has(item)) {
      return false; // Item already exists, insertion failed
    }

    this.backingSet.add(item);
    this.insertionOrder.push(item);
    return true; // Successful insertion
  }

  /**
   * Checks if an item exists in the journal.
   * @param item The item to check for
   * @returns true if the item exists, false otherwise
   */
  has(item: T): boolean {
    return this.backingSet.has(item);
  }

  /**
   * Returns the number of items in the journal.
   * @returns The size of the journal
   */
  get size(): number {
    return this.backingSet.size;
  }

  /**
   * Returns all items in the order they were successfully inserted.
   * @returns A copy of the insertion order array
   */
  getInsertionOrder(): readonly T[] {
    return [...this.insertionOrder];
  }

  /**
   * Returns all unique items as a Set.
   * @returns A copy of the backing set
   */
  getUniqueItems(): Set<T> {
    return new Set(this.backingSet);
  }

  /**
   * Checks if the journal is empty.
   * @returns true if the journal contains no items, false otherwise
   */
  isEmpty(): boolean {
    return this.backingSet.size === 0;
  }

  /**
   * Returns the first item that was inserted (if any).
   * @returns The first inserted item or undefined if empty
   */
  getFirst(): T | undefined {
    return this.insertionOrder[0];
  }

  /**
   * Returns the last item that was inserted (if any).
   * @returns The last inserted item or undefined if empty
   */
  getLast(): T | undefined {
    return this.insertionOrder[this.insertionOrder.length - 1];
  }

  /**
   * Returns an iterator that yields items in insertion order.
   */
  *[Symbol.iterator](): Iterator<T> {
    for (const item of this.insertionOrder) {
      yield item;
    }
  }

  /**
   * Returns a string representation of the journal.
   * @returns String representation showing insertion order
   */
  toString(): string {
    return `InsertOnlyJournal(${this.insertionOrder.length}) [${this.insertionOrder.join(', ')}]`;
  }

  /**
   * Creates a new journal from an iterable of items.
   * Items are inserted in the order they appear in the iterable.
   * Duplicate items are ignored (only the first occurrence is kept).
   * @param items The items to insert
   * @returns A new InsertOnlyJournal instance
   */
  static from<T>(items: Iterable<T>): InsertOnlyJournal<T> {
    const journal = new InsertOnlyJournal<T>();
    for (const item of items) {
      journal.insert(item);
    }
    return journal;
  }
}
