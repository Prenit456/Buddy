import test from 'node:test';
import assert from 'node:assert/strict';
import {prescriptionDraft,reviewedReminders,validateCareSettings,validDate} from './care.mjs';
test('prescription extraction preserves ambiguous instructions without inventing times',()=>{
 const draft=prescriptionDraft('Tab Example 5 mg - twice daily\nCapsule Demo 10 mg at 09:30 and 8:15 pm','2026-09-19');
 assert.equal(draft.items.length,2);assert.deepEqual(draft.items[0].times,[]);
 assert.deepEqual(draft.items[1].times,['09:30','20:15']);assert.equal(draft.items[0].confirmed,false);
 assert.throws(()=>reviewedReminders(draft.items));
});
test('only reviewed complete schedules can become reminders',()=>{
 const item={title:'Example only',detail:'Exact prescription text',confirmed:true,times:['09:00','21:00','09:00'],startDate:'2026-09-19',endDate:'2026-09-21'};
 assert.equal(reviewedReminders([item]).length,2);
 assert.throws(()=>reviewedReminders([{...item,endDate:''}]));
 assert.throws(()=>reviewedReminders([{...item,times:['25:30']}]));
 assert.throws(()=>reviewedReminders([{...item,confirmed:false}]));
});
test('invalid dates and empty recurrence never silently become daily',()=>{
 assert.equal(validDate('2026-02-30'),false);assert.equal(validDate('2028-02-29'),true);
 const settings={person:{name:'Test'},contacts:[],reminders:[{id:'x',title:'Example',time:'09:00',days:[]}]};
 assert.match(validateCareSettings(settings),/weekday/);
});
