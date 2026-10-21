import copy
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from analyze_gpu_memory_trace import reconstruct,positive_control,validate_trace_metadata


class GpuMemoryTraceContracts(unittest.TestCase):
    def events(self):
        rows=[{'id':33,'version':3,'qpc':1,'pid':17,'adapter':100,'handle':200,'bytes':4096}]
        for event_id in (367,371):
            for segment in (0,1):
                for time,old,new in ((2,0,4096),(4,4096,0)):
                    rows.append({'id':event_id,'version':0,'qpc':time,'pid':17,'adapter':100,'physical_adapter':0,'segment_group':segment,'old_bytes':old,'new_bytes':new})
        rows.append({'id':34,'version':3,'qpc':5,'pid':4,'adapter':100,'handle':200,'bytes':4096})
        return sorted(rows,key=lambda r:r['qpc'])

    def test_system_thread_destruction_is_joined_by_allocation_handle(self):
        summary,filtered,lifetimes=reconstruct(self.events(),17,10000000)
        self.assertTrue(summary['allocation_lifetime_reconciled']);self.assertEqual(summary['allocation_count'],1)
        self.assertEqual(summary['system_context_destructions'],1);self.assertEqual(summary['usage_local_peak_bytes'],4096)
        self.assertEqual(filtered[-1]['owner_pid'],17);self.assertEqual(lifetimes[0]['destroy_payload_pid'],4)

    def test_missing_or_inconsistent_lifetime_or_usage_is_rejected(self):
        for mutation in ('missing_destroy','duplicate_create','wrong_size','chain_gap','missing_zero','missing_segment','new_adapter','unknown_version','time_inversion'):
            rows=self.events()
            if mutation=='missing_destroy':rows.pop()
            if mutation=='duplicate_create':rows.insert(1,copy.deepcopy(rows[0]))
            if mutation=='wrong_size':rows[-1]['bytes']=8192
            if mutation=='chain_gap':rows[1]['old_bytes']=1
            if mutation=='missing_zero':rows=[r for r in rows if not (r['id']==367 and r.get('segment_group')==0 and r.get('new_bytes')==0)]
            if mutation=='missing_segment':rows=[r for r in rows if not (r['id']==371 and r.get('segment_group')==1)]
            if mutation=='new_adapter':rows[1]['adapter']=101
            if mutation=='unknown_version':rows[0]['version']=9
            if mutation=='time_inversion':rows.reverse()
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):reconstruct(rows,17,10000000)

    def test_loss_decode_error_or_unknown_clock_cannot_pass(self):
        good={'clock':'raw_QPC','clock_type':1,'qpc_frequency':10000000,'header_events_lost':0,'consumer_events_lost':0,'buffers_lost':0,'process_trace_status':0,'decode_failed':False}
        validate_trace_metadata(good)
        for field in ('header_events_lost','consumer_events_lost','buffers_lost','process_trace_status'):
            for value in (1,None,False,-1):
                with self.subTest(field=field,value=value),self.assertRaises(ValueError):validate_trace_metadata({**good,field:value})
        for bad in ({'clock_type':2},{'qpc_frequency':0},{'decode_failed':True},{'clock':'wall_time'}):
            with self.assertRaises(ValueError):validate_trace_metadata({**good,**bad})

    def control(self):
        size=32*1024*1024
        control={'revision':1,'kind':'transient_gpu_memory_positive_control','validation':True,'readback_verified':True,'reference_checks':3,'buffer_bytes':size,'simultaneous_buffer_count':2,'qpc_frequency':10000000,
                 'intervals':[{'before_qpc':10+i*10,'live_qpc':15+i*10,'after_qpc':19+i*10} for i in range(3)],
                 'telemetry':{'checkpoints':[{'phase':'baseline','sample':0,'gpu_local_current_bytes':0}]+[{'phase':'sample_end','sample':i,'gpu_local_current_bytes':0} for i in range(3)]}}
        events=[{'id':367,'qpc':12+i*10,'segment_group':0,'new_bytes':size*2} for i in range(3)]
        lifetimes=[{'create_qpc':11+i*10,'destroy_qpc':18+i*10,'bytes':size} for i in range(3) for _ in range(2)]
        return control,events,lifetimes

    def test_transient_positive_control_requires_overlap_inside_checkpoint_gap_and_missed_peak(self):
        c,e,l=self.control();self.assertEqual(len(positive_control(c,e,l,10000000)),3)
        for mutation in ('one_buffer','checkpoint_observed_peak','outside_gap','no_reference','wrong_clock'):
            c,e,l=self.control()
            if mutation=='one_buffer':l.pop()
            if mutation=='checkpoint_observed_peak':c['telemetry']['checkpoints'][0]['gpu_local_current_bytes']=64*1024*1024
            if mutation=='outside_gap':l[0]['destroy_qpc']=100
            if mutation=='no_reference':c['readback_verified']=False
            if mutation=='wrong_clock':c['qpc_frequency']=1
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):positive_control(c,e,l,10000000)


if __name__=='__main__':unittest.main()
